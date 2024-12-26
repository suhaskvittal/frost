/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#include "dram/cmd_queue.h"
#include "dram/state.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t RQ_SIZE = (3*DRAM_CMDQ_SIZE)/4;
constexpr size_t WQ_SIZE = DRAM_CMDQ_SIZE - RQ_SIZE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
CmdQueue::can_accept(bool is_write) const
{
#if defined(DRAM_CMDQ_SPLIT)
    return is_write ? (impl_.writes.size() < WQ_SIZE) : (impl_.reads.size() < RQ_SIZE);
#else
    return impl_.size() < DRAM_CMDQ_SIZE;
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
CmdQueue::has_no_pending_reads() const
{
#if defined(DRAM_CMDQ_SPLIT)
    return impl_.reads.empty();
#else
    return impl_.empty()
            || std::all_of(impl_.begin(), impl_.end(),
                    [] (const DRAMCommand& cmd) { return cmd_is_write(cmd.type); });
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
CmdQueue::enqueue(DRAMCommand&& cmd)
{
#if defined(DRAM_CMDQ_SPLIT)
    if (cmd_is_write(cmd.type))
        impl_.writes.push_back(cmd);
    else
        impl_.reads.push_back(cmd);
#else
    impl_.push_back(cmd);
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommand
CmdQueue::select_command(const DRAMChannelState& ch)
{
    // Select queue to issue command from:
#if defined(DRAM_CMDQ_SPLIT)
    // Check if we need to drain writes:
    if (impl_.writes_to_drain == 0 && impl_.writes.size() >= WQ_SIZE)
        impl_.writes_to_drain = impl_.writes.size();
    // If we need to drain writes, then select the write queue.
    auto& q = impl_.writes_to_drain > 0 ? impl_.writes : impl_.reads;
#else
    auto& q = impl_;
#endif

    bool any_reads = std::any_of(q.begin(), q.end(),
                            [] (const DRAMCommand& c)
                            {
                                return cmd_is_read(c.type);
                            });
    bool any_read_hits = std::any_of(q.begin(), q.end(),
                            [ch] (const DRAMCommand& c)
                            {
                                const auto& b = get_bank_state(ch, c.trans.address);
                                return cmd_is_read(c.type) 
                                        && b.open_row.has_value()
                                        && b.open_row == dram_row(c.trans.address);
                            });

    bool first = true;
    for (auto it = q.begin(); it != q.end(); it++) {
        DRAMCommand ready_cmd;
        uint64_t addr = it->trans.address; 

        if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::ARFCFS) {
            if (cmd_is_write(it->type) && any_reads)
                continue;
        }

        if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::FRRFCFS) {
            if (cmd_is_write(it->type) && any_read_hits)
                continue;
        }

        const auto& b = get_bank_state(ch, addr);
        size_t r = dram_row(addr);
        if (b.open_row.has_value()) {
            if (r == b.open_row)
                ready_cmd = *it;
            else if (allow_precharge(b, first, std::next(it), q.end()))
                ready_cmd = DRAMCommand(addr, DRAMCommandType::PRECHARGE);
        } else {
            ready_cmd = DRAMCommand(addr, DRAMCommandType::ACTIVATE);
        }

        if (!cmd_is_invalid(ready_cmd.type) && cmd_is_issuable(ch, ready_cmd)) {
            if (cmd_is_cas(ready_cmd.type)) {
#if defined(DRAM_CMDQ_SPLIT)
                if (cmd_is_write(ready_cmd.type))
                    --impl_.writes_to_drain;
#endif
                q.erase(it);
            } else {
                it->is_row_buffer_hit = false;
            }
            return ready_cmd;
        }

        first = false;
    }
    return DRAMCommand();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

CommandScheduler::CommandScheduler(const DRAMChannelState& s)
    :state_(s)
{}

bool
CommandScheduler::can_accept(uint64_t address, bool is_write) const
{
    size_t ii = get_bank_idx(address);
    return cmd_queues_[ii].can_accept(is_write);
}

bool
CommandScheduler::has_no_pending_reads() const
{
    return std::all_of(cmd_queues_.begin(), cmd_queues_.end(), 
                [] (const auto& q) { return q.has_no_pending_reads(); });
}

void
CommandScheduler::enqueue(DRAMCommand&& cmd)
{
    size_t ii = get_bank_idx(cmd.trans.address);
    return cmd_queues_[ii].enqueue(std::move(cmd));
}

DRAMCommand
CommandScheduler::select_command()
{
    DRAMCommand cmd;
    for (size_t i = 0; i < cmd_queues_.size(); i++) {
        auto& q = cmd_queues_[next_cmd_queue_idx_];
        fast_increment_and_mod_inplace<TOT_BANKS>(next_cmd_queue_idx_);
    
        cmd = q.select_command(state_);
        if (!cmd_is_invalid(cmd.type))
            break;
    }
    return cmd;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
allow_precharge(const DRAMBankState& b, bool is_first, cmdq_iterator next_begin, cmdq_iterator end)
{
    if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::FCFS) {
        return true;
    } else {
        bool any_pending_hits = std::any_of(next_begin, end,
                                    [b] (const DRAMCommand& cmd)
                                    {
                                        if constexpr (DRAM_CMDQ_POLICY == DRAMCmdQueuePolicy::ARFCFS) {
                                            if (cmd_is_write(cmd.type))
                                                return false;
                                        }
                                        return b.open_row == dram_row(cmd.trans.address);
                                    });
        return is_first && (!any_pending_hits || b.num_cas_to_open_row > 4);
    }
}

const DRAMBankState&
get_bank_state(const DRAMChannelState& ch, uint64_t address)
{
    size_t ra = dram_rank(address),
           bg = dram_bankgroup(address),
           ba = dram_bank(address);
    return ch.at(ra).at(bg).at(ba);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
