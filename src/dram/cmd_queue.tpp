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
__TEMPLATE_CLASS__::enqueue(Transaction&& trans, DRAMCommandType type)
{
    impl_.emplace_back(trans, type);

    if (cmd_is_write(type))
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

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::cmd_output_t
__TEMPLATE_CLASS__::select_command(const DRAMChannelState& ch, bool force_write)
{
    if constexpr (WPOL == DRAMWritePolicy::ALAP)
        alap_update_write_mode();

    DRAMCommand ready_cmd;
    std::optional<CmdQueueEntry> opt_e; // Should only have a value if we are doing a R/W

    bool first = true;
    for (auto cmd_it = impl_.begin(); cmd_it != impl_.end(); cmd_it++)
    {
        uint64_t addr = cmd_it->trans.address;
        DRAMCommandType type = DRAMCommandType::INVALID;

        if constexpr (WPOL == DRAMWritePolicy::ALAP)
        {
            // If we are not draining but the command is a write -- skip.
            if ((writes_to_drain_ == 0) == cmd_is_write(cmd_it->type))
                continue;
        }
        else if constexpr (WPOL == DRAMWritePolicy::ALAP_SYNC)
        {
            if (force_write && !cmd_is_write(cmd_it->type))
                continue;
            if (!force_write && cmd_is_write(cmd_it->type))
                continue;
        }

        const auto& b = get_bank_ref(ch, addr);
        if (b.open_row.has_value())
        {
            if (b.open_row == dram_row(addr))
                type = cmd_it->type;
            else if (allow_demand_precharge(b, first, cmd_it, impl_.end()))
                type = DRAMCommandType::PRECHARGE;
        }
        else
            type = DRAMCommandType::ACTIVATE;

        // Now that we have `type` (may or may not be invalid), create the ready command.
        ready_cmd.address = addr;
        ready_cmd.type = type;
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
                cmd_it->is_row_buffer_hit = b.next_cas_is_row_buffer_hit;
                opt_e.emplace(std::move(*cmd_it));
                impl_.erase(cmd_it);
            }
            break;  // Exit the loop and return.
        }
        else
            ready_cmd.type = DRAMCommandType::INVALID;

        first = false;
    }
    return cmd_output_t(ready_cmd, opt_e);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::print_queue_contents(std::ostream& out)
{
    std::string cmd_string;
    for (const auto& cmd : impl_)
    {
        if (cmd_is_read(cmd.type))
            cmd_string += " RD";
        else
            cmd_string += " WR";
    }

    out << std::setw(3*SIZE) << std::left << cmd_string 
        << "size: " << std::setw(2) << std::right << size()
        << " of " << SIZE << "(R:W = " << num_reads() << ":" << num_writes() << ")";
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

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::alap_update_write_mode()
{
    bool drain_cond_1 = num_reads() == 0 && num_writes() > ALAP_MAX_WRITES,
         drain_cond_2 = size() == SIZE && num_writes() > ALAP_MAX_WRITES;
    if (drain_cond_1 || drain_cond_2)
        writes_to_drain_ = num_writes();
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
        bool any_pending_hits = 
            std::any_of(std::next(cmd_it), end,
                    [b, 
                    base_cmd_is_read=trans_is_read(cmd_it->trans.type),
                    bank_idx=get_bank_idx(cmd_it->trans.address)] 
                    (const auto& cmd)
                    {
                        if constexpr (WPOL == DRAMWritePolicy::ALAP || WPOL == DRAMWritePolicy::ALAP_SYNC)
                        {
                            if (trans_is_read(cmd.trans.type) != base_cmd_is_read)
                                return false;
                        }
                        return bank_idx == get_bank_idx(cmd.trans.address)
                                && b.open_row == dram_row(cmd.trans.address);
                    });
        return is_first && (!any_pending_hits || b.num_cas_to_open_row >= 4);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_CLASS__
#undef __TEMPLATE_HEADER__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
