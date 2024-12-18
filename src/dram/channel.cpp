/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "globals.h"
#include "dram_timing.h"

#include "dram/address.h"
#include "dram/channel.h"
#include "io_bus.h"
#include "transaction.h"
#include "util/numerics.h"

#include <iomanip>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr DRAMCommandType READ_CMD = (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
                                        ? DRAMCommandType::READ
                                        : DRAMCommandType::READ_PRECHARGE;

constexpr DRAMCommandType WRITE_CMD = (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
                                        ? DRAMCommandType::WRITE
                                        : DRAMCommandType::WRITE_PRECHARGE;

constexpr size_t BL = DRAM_BURST_LENGTH;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
//
// Timing update functions

inline void update(uint64_t& t, uint64_t nt)
{
    t = std::max(t, GL_DRAM_CYCLE+nt);
}

inline void update_SL(std::array<uint64_t,2>& t, uint64_t diff, uint64_t same)
{
    update(t[0], diff);
    update(t[1], same);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMChannel::DRAMChannel(double freq_ghz)
    :io_(new IOBus(DRAM_RQ_SIZE, DRAM_WQ_SIZE, 0)),
    freq_ghz_(freq_ghz),
    next_ref_cycle_(tREFI)
{}

DRAMChannel::~DRAMChannel() {}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::tick_mc()
{
    schedule_next_cmd();
}

void
DRAMChannel::tick_dram()
{
    // Update FAW:
    while (!faw_.empty() && GL_DRAM_CYCLE >= faw_.front() + tFAW)
        faw_.pop_front();

    if (GL_DRAM_CYCLE >= next_ref_cycle_) {
        // Handle refresh
        bool all_ready = true;
        for (auto& b : banks_) {
            if (b.open_row_.has_value()) {
                all_ready = false;
                if (GL_DRAM_CYCLE >= b.pre_ok_cycle_) {
                    bank_update_pre(b);
                }
            } else {
                all_ready &= GL_DRAM_CYCLE >= b.act_ok_cycle_;
            }
        }

        if (all_ready) {
            next_ref_cycle_ = GL_DRAM_CYCLE + tREFI;
            ref_done_cycle_ = GL_DRAM_CYCLE + tRFC;
            ++s_refreshes_;
        }
    } else {
        if (GL_DRAM_CYCLE >= ref_done_cycle_)
            issue_next_cmd();
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::schedule_next_cmd()
{
    auto tt = io_->get_next_incoming(
                    [this] (const Transaction& t)
                    {
                        return this->get_bank(t.address).cmd_queue_.size() < DRAM_CMDQ_SIZE;
                    });
    if (tt.has_value()) {
        Transaction& t = tt.value();
        auto& b = get_bank(t.address);
        b.cmd_queue_.emplace_back(t, trans_is_read(t.type) ? READ_CMD : WRITE_CMD);
        if (trans_is_read(t.type)) {
            b.cmd_queue_.emplace_back(t, READ_CMD);
        } else {
            b.cmd_queue_.emplace_back(t, WRITE_CMD);
            ++b.num_writes_in_cmdq_;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::issue_next_cmd()
{
    auto _ready_cmd = select_next_command();
    if (!_ready_cmd.has_value())
        return;
    DRAMCommand& ready_cmd = _ready_cmd.value();
    update_timing(ready_cmd);
    if (cmd_is_read(ready_cmd.type)) {
        // Mark as outgoing.
        io_->add_outgoing(ready_cmd.trans, CL);
        // Update stats.
        uint64_t read_latency = GL_DRAM_CYCLE - ready_cmd.cycle_entered_cmd_queue; 
        if (last_cmd_was_read_) {
            s_tot_read_after_read_latency_ += read_latency;
            ++s_num_read_after_read_;
        } else {
            s_tot_read_after_write_latency_ += read_latency;
            ++s_num_read_after_write_;
        }
        last_cmd_was_read_ = true;
    } else if (cmd_is_write(ready_cmd.type)) {
        last_cmd_was_read_ = false;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMChannel::cmd_is_issuable(const DRAMCommand& cmd)
{
    const auto& b = get_bank(cmd.trans.address);
    DRAMCommandType c = cmd.type;
    // Check bank level constraints.
    if (cmd_is_cas(c) && GL_DRAM_CYCLE < b.cas_ok_cycle_)
        return false;
    else if (c == DRAMCommandType::PRECHARGE && GL_DRAM_CYCLE < b.pre_ok_cycle_)
        return false;
    else if (c == DRAMCommandType::ACTIVATE && GL_DRAM_CYCLE < b.act_ok_cycle_)
        return false;
    // Now check channel level constraints.
    size_t ii = static_cast<size_t>(dram_bankgroup(cmd.trans.address) == last_bankgroup_);
    if (cmd_is_read(c) && GL_DRAM_CYCLE < rd_ok_cycle_[ii])
        return false;
    if (cmd_is_write(c) && GL_DRAM_CYCLE < wr_ok_cycle_[ii])
        return false;
    if (c == DRAMCommandType::ACTIVATE && (GL_DRAM_CYCLE < act_ok_cycle_[ii] || faw_.size() == 4))
        return false;
    // Otherwise, the command meets all criteria.
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::update_timing(const DRAMCommand& cmd)
{
    auto& b = get_bank(cmd.trans.address);
    DRAMCommandType c = cmd.type;
    // Bank level updates
    if (cmd_is_cas(c))
        bank_update_cas(b, cmd_is_read(c), cmd_is_autopre(c));
    else if (c == DRAMCommandType::ACTIVATE)
        bank_update_act(b, dram_row(cmd.trans.address));
    else
        bank_update_pre(b);
    // Now do channel-level updates
    switch (c) {
    case DRAMCommandType::READ:
    case DRAMCommandType::READ_PRECHARGE:
        update_SL(rd_ok_cycle_, tCCD_S, tCCD_L);
        update_SL(wr_ok_cycle_, tCCD_S_RTW, tCCD_L_RTW);
        break;
    case DRAMCommandType::WRITE:
    case DRAMCommandType::WRITE_PRECHARGE:
        update_SL(rd_ok_cycle_, tCCD_S_WTR, tCCD_L_WTR);
        update_SL(wr_ok_cycle_, tCCD_S_WR, tCCD_L_WR);
        break;
    case DRAMCommandType::ACTIVATE:
        update_SL(act_ok_cycle_, tRRD_S, tRRD_L);
        faw_.push_back(GL_DRAM_CYCLE);
        break;
    default:
        break;
    }
    last_bankgroup_ = dram_bankgroup(cmd.trans.address);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMChannel::sel_cmd_t
DRAMChannel::select_next_command()
{
    sel_cmd_t out;
    for (size_t i = 0; i < banks_.size(); i++) {
        auto& b = banks_[next_bank_with_cmd_];
        fast_increment_and_mod_inplace<TOT_BANKS>(next_bank_with_cmd_);
        
        if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::ARRFCFS) {
            if (b.write_draining_ == 0 && b.num_writes_in_cmdq_ == b.cmd_queue_.size())
                b.write_draining_ = b.cmd_queue_.size();
        }

        for (auto cmd_it = b.cmd_queue_.begin(); cmd_it != b.cmd_queue_.end(); cmd_it++) {
            if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::FCFS)
                out = FCFS(cmd_it, b);
            else if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::FRFCFS)
                out = FRFCFS(cmd_it, b);
            else if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::FRRFCFS)
                out = FRRFCFS(cmd_it, b);
            else if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::ARRFCFS)
                out = ARRFCFS(cmd_it, b);
            else {
                std::cerr << "dram: unknown cmd queue scheduling policy.\n";
                exit(1);
            }

            if (out.has_value() && cmd_is_issuable(out.value())) {
                if (cmd_is_cas(out.value().type)) {
                    if (cmd_is_write(cmd_it->type)) {
                        --b.num_writes_in_cmdq_;
                        --b.write_draining_;
                    }

                    if (cmd_it->is_row_buffer_hit)
                        ++s_row_buffer_hits_;

                    b.cmd_queue_.erase(cmd_it);
                } else {
                    cmd_it->is_row_buffer_hit = false;
                    if (out.value().type == DRAMCommandType::PRECHARGE)
                        ++s_pre_demand_;
                }
                return out;
            }
        }
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMBank&
DRAMChannel::get_bank(uint64_t addr)
{
    return banks_.at(get_bank_idx(addr));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::bank_update_act(DRAMBank& b, uint64_t row)
{
    b.open_row_ = row;
    update(b.cas_ok_cycle_, tRCD);
    update(b.pre_ok_cycle_, tRAS);
    ++s_activates_;
}

void
DRAMChannel::bank_update_cas(DRAMBank& b, bool is_read, bool autopre)
{
    uint64_t cas_to_pre = is_read ? tRTP : (BL/2 + CWL + tWR);
    if (autopre) {
        b.open_row_.reset();
        update(b.act_ok_cycle_, cas_to_pre + tRP);

        ++s_precharges_;
    } else {
        ++b.num_cas_to_open_row_;
        update(b.pre_ok_cycle_, cas_to_pre);
    }
    if (is_read)
        ++s_reads_;
    else
        ++s_writes_;
}

void
DRAMChannel::bank_update_pre(DRAMBank& b)
{
    b.open_row_.reset();
    b.num_cas_to_open_row_ = 0;

    update(b.act_ok_cycle_, tRP);

    ++s_precharges_;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
