/*
 *  author: Suhas Vittal
 *  date:   25 December 2024
 * */

#include <algorithm>
#include <iostream>
#include <iomanip>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <DRAMSchedPolicy SPOL, size_t SIZE, bool ASSUME_BANK_SPECIFIC, DRAMWritePolicy WPOL>
#define __TEMPLATE_CLASS__  CmdQueue<SPOL, SIZE, ASSUME_BANK_SPECIFIC, WPOL>
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::enqueue(DRAMCommand&& cmd)
{
    impl_.push_back(cmd);

    if (cmd_is_write(cmd.type))
    {
        ++writes_in_queue_;
        if constexpr (WPOL == DRAMWritePolicy::ALAP)
        {
            if (writes_to_drain_ == 0 && writes_in_queue_ >= ALAP_MAX_WRITES)
                writes_to_drain_ = writes_in_queue_;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ DRAMCommand
__TEMPLATE_CLASS__::select_command(const DRAMChannelState& ch, bool force_write)
{
    bool first = true;
    for (auto cmd_it = impl_.begin(); cmd_it != impl_.end(); cmd_it++)
    {
        DRAMCommand ready_cmd;
        uint64_t addr = cmd_it->trans.address;
        
        if (force_write && !cmd_is_write(cmd_it->type))
            continue;

        if constexpr (WPOL == DRAMWritePolicy::ALAP)
        {
            // If we are not draining but the command is a write -- skip.
            if ((writes_to_drain_ == 0) == cmd_is_write(cmd_it->type))
                continue;
        }
        else if constexpr (WPOL == DRAMWritePolicy::ALAP_SYNC)
        {
            if (!force_write && cmd_is_write(cmd_it->type))
                continue;
        }

        const auto& b = get_bank_ref(ch, addr);
        if (b.open_row.has_value())
        {
            if (b.open_row == dram_row(addr))
                ready_cmd = std::move(*cmd_it);
            else if (allow_demand_precharge(b, first, cmd_it, impl_.end()))
                ready_cmd = DRAMCommand(addr, DRAMCommandType::PRECHARGE);
        }
        else
            ready_cmd = DRAMCommand(addr, DRAMCommandType::ACTIVATE);
        // Check if `ready_cmd` is issuable; if so, return it.
        if (!cmd_is_invalid(ready_cmd.type) && cmd_is_issuable(ch, ready_cmd))
        {
            if (cmd_is_cas(ready_cmd.type))
            {
                if (cmd_is_write(ready_cmd.type))
                {
                    --writes_in_queue_;
                    if constexpr (WPOL == DRAMWritePolicy::ALAP)
                        --writes_to_drain_;
                }
                impl_.erase(cmd_it);
            }
            else
                cmd_it->is_row_buffer_hit = false;
            return ready_cmd;
        }
        else if (cmd_is_cas(ready_cmd.type))
            *cmd_it = std::move(ready_cmd);

        first = false;
    }
    return DRAMCommand();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::print_queue_contents(std::ostream& out)
{
    std::string cmd_string;
    for (const DRAMCommand& cmd : impl_)
    {
        if (cmd_is_read(cmd.type))
            cmd_string += " RD";
        else
            cmd_string += " WR";
    }
    
    size_t num_reads = std::count_if(impl_.begin(), impl_.end(),    
                            [] (const DRAMCommand& cmd)
                            {
                                return cmd_is_read(cmd.type);
                            }),
           num_writes = std::count_if(impl_.begin(), impl_.end(),
                            [] (const DRAMCommand& cmd)
                            {
                                return cmd_is_write(cmd.type);
                            });

    out << std::setw(3*SIZE) << std::left << cmd_string 
        << "size: " << std::setw(2) << std::right << size()
        << " of " << SIZE << "(R:W = " << num_reads << ":" << num_writes << ")";
    if constexpr (ASSUME_BANK_SPECIFIC)
    {
        out << std::setw(16) << std::right << "bankstate:"
            << " row open: " << (bank_p_->open_row.has_value() ? "y" : "n")
            << ", act ok: " << bank_p_->act_ok
            << ", pre ok: " << bank_p_->pre_ok
            << ", cas_ok: " << bank_p_->cas_ok
            << ", num cas to open row: " << bank_p_->num_cas_to_open_row;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline const DRAMBankState&
__TEMPLATE_CLASS__::get_bank_ref(const DRAMChannelState& ch, uint64_t address)
{
    if constexpr (ASSUME_BANK_SPECIFIC)
        return *bank_p_;
    else
        return get_bank_state(ch, address);
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
    if constexpr (SPOL == DRAMSchedPolicy::FCFS)
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
