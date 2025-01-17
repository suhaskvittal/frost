/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#include "dram/address.h"
#include "dram/channel.h"
#include "dram/enums.h"
#include "dram/scheduler.h"
#include "dram/state.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
scheduler_allow_demand_precharge(
        queue_type::const_iterator q_it,
        const queue_type& q,
        const SchedulerState& s,
        const DRAMBankState& bank)
{
    if constexpr (DRAM_SCHED_POLICY == DRAMSchedPolicy::FCFS)
        return true;
    else
    {
        size_t bank_idx = dram_bank_idx(q_it->trans.address);
        bool any_pending_row_hits = std::any_of(std::next(q_it), q.end(),
                                        [bank_idx, row=bank.open_row.value()] (const auto& e)
                                        {
                                            return dram_bank_idx(e.trans.address) == bank_idx
                                                    && dram_row(e.trans.address) == row;
                                        });
        return s.is_first.at(bank_idx) && (!any_pending_row_hits || bank.num_cas_to_open_row >= 4);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommandType
scheduler_get_cas_command(
        queue_type::const_iterator q_it,
        const queue_type& q,
        const SchedulerState& s, 
        const DRAMBankState& bank)
{
    bool is_write = s.is_write_mode;
    
    if (is_write && q_it->trans.dram_write_hint_valid)
    {
        if (q_it->trans.dram_write_hint_do_autopre)
            return DRAMCommandType::WRITE_PRECHARGE;
        else
            return DRAMCommandType::WRITE;
    }
    else
    {
        if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
            return is_write ? DRAMCommandType::WRITE : DRAMCommandType::READ;
        else if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::CLOSE)
            return is_write ? DRAMCommandType::WRITE_PRECHARGE : DRAMCommandType::READ_PRECHARGE;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
