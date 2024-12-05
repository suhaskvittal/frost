/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

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

DRAMChannel::DRAMChannel(double freq_ghz, std::string dram_type)
    :freq_ghz_(freq_ghz),
    io_(new IOBus(DRAM_RQ_SIZE, DRAM_WQ_SIZE, 0))
{
    // Fill in timings based on `dram_type`.
    if (dram_type == "4800") {
        CL = ckcast(16.0);
        CWL = CL - 2;
        tRCD = ckcast(16.0);
        tRP = ckcast(16.0);
        tRAS = ckcast(32.0);
        tRTP = std::max(12, ckcast(7.5));
        tWR = ckcast(30);

        tCCD_S = 8;
        tCCD_S_WR = 8;
        tCCD_S_WTR = CWL + BL/2 + std::max(4, ckcast(2.5));
        tCCD_S_RTW = tCCD_S_WTR;

        tCCD_L = std::max(8, ckcast(5.0));
        tCCD_L_WR = std::max(32, ckcast(20.0));
        tCCD_L_WTR = CWL + BL/2 + std::max(16, ckcast(10.0));
        tCCD_L_RTW = tCCD_L_WTR;

        tRRD_S = 8;
        tRRD_L = std::max(8, ckcast(5.0));
        tFAW = std::max(32, ckcast(13.333));
        tRFC = ckcast(410.0);
        tREFI = ckcast(32e3 / 8192);
    }
    last_ref_cycle_ = tREFI;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::tick()
{
    // Update FAW:
    while (!faw_.empty() && faw_.front() <= GL_DRAM_CYCLE + tFAW)
        faw_.pop_front();

    if (GL_DRAM_CYCLE >= last_ref_cycle_) {
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

inline void
list(std::ostream& out, std::string_view name, uint64_t ck, double tCK)
{
    double time_ns = ck * tCK;
    out << std::setw(12) << std::left << name
        << std::setw(12) << std::left << std::setprecision(3) << time_ns << "ns"
        << std::setw(12) << std::left << ck << " cycles\n";
}

void
DRAMChannel::list_dram_timings(std::ostream& out)
{
    const std::string_view BAR = "------------------------------------------------------------------------";

    double tCK = 1.0/freq_ghz_;

    out << BAR << "\n"
        << "DRAM frequency = " << freq_ghz_ << "GHz, tCK = " << std::setprecision(5) << tCK << "\n"
        << BAR << "\n"
        << std::setw(12) << std::left << "DRAM TIMING"
        << std::setw(12) << std::left << "ns"
        << std::setw(12) << std::left << "nCK\n"
    list(out, "CL", CL, tCK);
    list(out, "CWL", CWL, tCK);
    list(out, "tRCD", tRCD, tCK);
    list(out, "tRP", tRP, tCK);
    list(out, "tRAS", tRAS, tCK);
    list(out, "tRTP", tRTP, tCK);
    list(out, "tWR", tWR, tCK);

    out << "\n";

    list(out, "tCCD_S", tCCD_S, tCK);
    list(out, "tCCD_S_WR", tCCD_S_WR, tCK);
    list(out, "tCCD_S_WTR", tCCD_S_WTR, tCK);
    list(out, "tCCD_S_RTW", tCCD_S_RTW, tCK);

    out << "\n";

    list(out, "tCCD_L", tCCD_L, tCK);
    list(out, "tCCD_L_WR", tCCD_L_WR, tCK);
    list(out, "tCCD_L_WTR", tCCD_L_WTR, tCK);
    list(out, "tCCD_L_RTW", tCCD_L_RTW, tCK);

    out << "\n";

    list(out, "tRRD_S", tRRD_S, tCK);
    list(out, "tRRD_L", tRRD_L, tCK);
    list(out, "tFAW", tFAW, tCK);

    out << "\n";

    list(out, "tRFC", tRFC, tCK);
    list(out, "tREFI", tREFI, tCK);

    out << BAR << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::schedule_next_cmd()
{
    IOBus::trans_t tt = io_->get_next_incoming(
                                [this] (const Transaction& t)
                                {
                                    return this->get_bank(t.address).cmd_queue_.size() < DRAM_CMDQ_SIZE;
                                });
    if (tt.has.value()) {
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
    else if (c == DRAMCommandType::PRECHARGE && GL_DRAM_CYCLE < b.pre_ok_cycle)
        return false;
    else if (c == DRAMCommandType::ACTIVATE && GL_DRAM_CYCLE < b.act_ok_cycle)
        return false;
    // Now check channel level constraints.
    size_t ii = static_cast<size_t>(dram_bankgroup(c.trans.address) == last_bankgroup_);
    if (is_read(c) && GL_DRAM_CYCLE < rd_ok_cycle[ii])
        return false;
    if (is_write(c) && GL_DRAM_CYCLE < wr_ok_cycle[ii])
        return false;
    if (c == DRAMCommandType::ACTIVATE && (GL_DRAM_CYCLE < act_ok_cycle[ii] || faw_.size() == 4))
        return false;
    // Otherwise, the command meets all criteria.
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::update_timing(const DRAMCommand& cmd)
{
    const auto& b = get_bank(cmd.trans.address);
    DRAMCommandType c = cmd.type;
    // Bank level updates
    switch (c) {
    case DRAMCommandType::READ:
        update(b.pre_ok_cycle, tRTP);
        ++b.num_cas_to_open_row_;
        break;
    case DRAMCommandType::READ_PRECHARGE:
        update(b.act_ok_cycle, BL/2 + tRTP + tRP);
        b.open_row_.reset();
        break;
    case DRAMCommandType::WRITE:
        update(b.pre_ok_cycle, CWL + BL/2 + tWR);
        ++b.num_cas_to_open_row_;
        break;
    case DRAMCommandType::WRITE_PRECHARGE:
        update(b.act_ok_cycle, CWL + BL/2 + tWR + tRP);
        b.open_row_.reset();
        break;
    case DRAMCommandType::ACTIVATE:
        update(b.cas_ok_cycle, tRCD);
        update(b.pre_ok_cycle, tRP);
        b.open_row_ = dram_row(cmd.trans.address);
        break;
    case DRAMCommandType::PRECHARGE:
        update(b.act_ok_cycle, tRP);
        b.open_row_.reset();
        break;
    }
    // Now do channel-level updates
    switch (c) {
    case DRAMCommandType::READ:
    case DRAMCommandType::READ_PRECHARGE:
        update_SL(rd_ok_cycle, tCCD_S, tCCD_L);
        update_SL(wr_ok_cycle, tCCD_S_RTW, tCCD_L_RTW);
        break;
    case DRAMCommandType::WRITE:
    case DRAMCommandType::WRITE_PRECHARGE:
        update_SL(rd_ok_cycle, tCCD_S_WTR, tCCD_L_WTR);
        update_SL(wr_ok_cycle, tCCD_S_WR, tCCD_L_WR);
        break;
    case DRAMCommandType::ACTIVATE:
        update_SL(act_ok_cycle, tRRD_S, tRRD_L);
        faw_.push_back(GL_DRAM_CYCLE);
        break;
    default:
        break;
    }
    last_bankgroup_used_ = dram_bankgroup(cmd.trans.address);
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
