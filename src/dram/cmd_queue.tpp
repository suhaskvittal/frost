/*
 *  author: Suhas Vittal
 *  date:   25 December 2024
 * */

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <DRAMSchedPolicy POL, size_t SIZE, bool ASSUME_BANK_SPECIFIC, bool QUEUE_IS_SPLIT>
#define __TEMPLATE_CLASS__  CmdQueue<POL, SIZE, ASSUME_BANK_SPECIFIC, QUEUE_IS_SPLIT>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::can_accept(bool write) const
{
    if constexpr (QUEUE_IS_SPLIT) 
        return write ? impl_.reads.size() < RQ_SIZE : impl_.writes.size() < WQ_SIZE;
    else
        return impl_.size() < SIZE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::has_no_pending_reads() const
{
    if constexpr (QUEUE_IS_SPLIT)
        return impl_.reads.empty();
    else
        return impl_.empty() || std::all_of(impl_.begin(), impl_.end(), 
                                    [] (const DRAMCommand& cmd)
                                    {
                                        return cmd_is_write(cmd.type);
                                    });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline size_t
__TEMPLATE_CLASS__::size() const
{
    if constexpr (QUEUE_IS_SPLIT)
        return impl_.reads.size() + impl_.writes.size();
    else
        return impl_.size();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::enqueue(DRAMCommand&& cmd)
{
    if constexpr (QUEUE_IS_SPLIT)
    {
        if (cmd_is_write(cmd.type))
            impl_.writes.push_back(cmd);
        else
            impl_.reads.push_back(cmd);
    }
    else
    {
        impl_.push_back(cmd);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ DRAMCommand
__TEMPLATE_CLASS__::select_command(const DRAMChannelState& ch)
{
    queue_t& q = get_queue_ref();
    
    bool first = true;
    for (auto cmd_it = q.begin(); cmd_it != q.end(); cmd_it++)
    {
        DRAMCommand ready_cmd;
        uint64_t addr = cmd_it->trans.address;

        const auto& b = get_bank_ref(ch, addr);
        if (b.open_row.has_value())
        {
            if (b.open_row == dram_row(addr))
                ready_cmd = *cmd_it;
            else if (allow_demand_precharge(b, first, cmd_it, q.end()))
                ready_cmd = DRAMCommand(addr, DRAMCommandType::PRECHARGE);
        }
        else
            ready_cmd = DRAMCommand(addr, DRAMCommandType::ACTIVATE);
        // Check if `ready_cmd` is issuable; if so, return it.
        if (!cmd_is_invalid(ready_cmd.type) && cmd_is_issuable(ch, ready_cmd))
        {
            if (cmd_is_cas(ready_cmd.type))
            {
                if constexpr (QUEUE_IS_SPLIT)
                {
                    if (cmd_is_write(ready_cmd.type))
                        --impl_.writes_to_drain;
                }
                q.erase(cmd_it);
            }
            else
                cmd_it->is_row_buffer_hit = false;
            return ready_cmd;
        }

        first = false;
    }
    return DRAMCommand();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::queue_t&
__TEMPLATE_CLASS__::get_queue_ref()
{
    if constexpr (QUEUE_IS_SPLIT)
    {
        // Check if writes need to be drained.
        if (impl_.writes_to_drain == 0 && impl_.writes.size() == WQ_SIZE)
            impl_.writes_to_drain = impl_.writes.size();
        return impl_.writes_to_drain > 0 ? impl_.writes : impl_.reads;
    } 
    else
        return impl_;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline const DRAMBankState&
__TEMPLATE_CLASS__::get_bank_ref(const DRAMChannelState& ch, uint64_t address)
{
    if constexpr (ASSUME_BANK_SPECIFIC)
        return *bank_p_;
    else
    {
        size_t ra = dram_rank(address),
               bg = dram_bankgroup(address),
               ba = dram_bank(address);
        return ch.at(ra).at(bg).at(ba);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::allow_demand_precharge(
        const DRAMBankState& b, 
        bool is_first, 
        queue_t::iterator cmd_it,
        queue_t::iterator end)
{
    if constexpr (POL == DRAMSchedPolicy::FCFS)
        return true;
    else
    {
        bool any_pending_hits = std::any_of(std::next(cmd_it), end,
                                    [b] (const DRAMCommand& cmd)
                                    {
                                        return b.open_row == cmd.trans.address;
                                    });
        return is_first && (!any_pending_hits || b.num_cas_to_open_row > 4);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_CLASS__
#undef __TEMPLATE_HEADER__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
