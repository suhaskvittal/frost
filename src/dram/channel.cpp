/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "globals.h"
#include "dram_timing.h"

#include "dram/channel.h"
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
    :freq_ghz_(freq_ghz),
    io_(new IOBus(DRAM_RQ_SIZE, DRAM_WQ_SIZE, 0)),
    next_ref_cycle_(tREFI)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::tick()
{
    // Update FAW:
    while (!faw_.empty() && faw_.front() <= GL_DRAM_CYCLE + tFAW)
        faw_.pop_front();

    if (GL_DRAM_CYCLE >= next_ref_cycle_) {
        // Handle refresh
        bool all_ready = true;
        for (auto& b : banks_) {
            if (b.open_row_.has_value()) {
                all_ready = false;
                if (GL_DRAM_CYCLE >= b.pre_ok_cycle_) {
                    b.open_row_.reset();
                    update(b.act_ok_cycle_, tRP);
                }
            } else {
                all_ready &= GL_DRAM_CYCLE >= b.act_ok_cycle_;
            }
        }

        if (all_ready) {
            next_ref_cycle_ = GL_DRAM_CYCLE + tREFI;
            ref_done_cycle_ = GL_DRAM_CYCLE + tRFC;
        }
    } else {
        // Cannot proceed if we are blocked by REF.
        if (GL_DRAM_CYCLE < ref_done_cycle_)
            return;
        // Otherwise, pretty straightforward:
        issue_next_cmd();
        schedule_next_cmd();
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
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::issue_next_cmd()
{
    auto _ready_cmd = frfcfs();
    if (!_ready_cmd.has_value())
        return;
    DRAMCommand& ready_cmd = _ready_cmd.value();
    // Check if the command is good.
    if (cmd_is_issuable(ready_cmd)) {
        update_timing(ready_cmd);
        if (is_read(ready_cmd.type)) {
            // Mark as outgoing.
            io_->outgoing_queue_.emplace(ready_cmd.trans, GL_CYCLE);
        }
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
    if ((is_read(c) || is_write(c)) && GL_DRAM_CYCLE < b.cas_ok_cycle_)
        return false;
    else if (c == DRAMCommandType::PRECHARGE && GL_DRAM_CYCLE < b.pre_ok_cycle_)
        return false;
    else if (c == DRAMCommandType::ACTIVATE && GL_DRAM_CYCLE < b.act_ok_cycle_)
        return false;
    // Now check channel level constraints.
    size_t ii = static_cast<size_t>(dram_bankgroup(cmd.trans.address) == last_bankgroup_);
    if (is_read(c) && GL_DRAM_CYCLE < rd_ok_cycle_[ii])
        return false;
    if (is_write(c) && GL_DRAM_CYCLE < wr_ok_cycle_[ii])
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
    switch (c) {
    case DRAMCommandType::READ:
        update(b.pre_ok_cycle_, tRTP);
        ++b.num_cas_to_open_row_;
        break;
    case DRAMCommandType::READ_PRECHARGE:
        update(b.act_ok_cycle_, BL/2 + tRTP + tRP);
        b.open_row_.reset();
        break;
    case DRAMCommandType::WRITE:
        update(b.pre_ok_cycle_, CWL + BL/2 + tWR);
        ++b.num_cas_to_open_row_;
        break;
    case DRAMCommandType::WRITE_PRECHARGE:
        update(b.act_ok_cycle_, CWL + BL/2 + tWR + tRP);
        b.open_row_.reset();
        break;
    case DRAMCommandType::ACTIVATE:
        update(b.cas_ok_cycle_, tRCD);
        update(b.pre_ok_cycle_, tRP);
        b.open_row_ = dram_row(cmd.trans.address);
        break;
    case DRAMCommandType::PRECHARGE:
        update(b.act_ok_cycle_, tRP);
        b.open_row_.reset();
        break;
    }
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
DRAMChannel::frfcfs()
{
    sel_cmd_t out;
    for (size_t i = 0; i < banks_.size(); i++) {
        auto& b = banks_[next_bank_with_cmd_];
        numeric_traits<TOT_BANKS>::increment_and_mod(next_bank_with_cmd_);
        // Search for first cmd queue entry with row buffer hit. 
        for (auto cmd_it = b.cmd_queue_.begin(); cmd_it != b.cmd_queue_.end(); cmd_it++) {
            DRAMCommand ready_cmd;
            // Get command that must be done to perform the R/W
            uint64_t addr = cmd_it->trans.address;
            size_t r = dram_row(addr);
            if (b.open_row_.has_value()) {
                if (r == b.open_row_) {
                    ready_cmd = *cmd_it;
                } else {
                    // Row buffer miss: check precharge conditions.
                    if (cmd_it != b.cmd_queue_.begin()) continue;
                    size_t num_pending = std::count_if(
                                                std::next(cmd_it),
                                                b.cmd_queue_.end(),
                                                [b] (DRAMCommand& c)
                                                {
                                                    return b.open_row_ == dram_row(c.trans.address);
                                                });
                    if (num_pending > 0 && b.num_cas_to_open_row_ < 4)
                        continue;
                    // Otherwise we are good:
                    ready_cmd = DRAMCommand(addr, DRAMCommandType::PRECHARGE);
                }
            } else {
                ready_cmd = DRAMCommand(addr, DRAMCommandType::ACTIVATE);
            }
            if (cmd_is_issuable(ready_cmd)) {
                // Success! return the command.
                out = ready_cmd;
                if (is_read(ready_cmd.type) || is_write(ready_cmd.type)) {
                    if (cmd_it->is_row_buffer_hit)
                        ++s_row_buffer_hits_;
                    b.cmd_queue_.erase(cmd_it);
                } else {
                    cmd_it->is_row_buffer_hit = false;
                }
                return out;
            }
        }
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
