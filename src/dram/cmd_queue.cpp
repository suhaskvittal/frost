/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#include "dram/cmd_queue.h"
#include "util/numerics.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

CommandScheduler::CommandScheduler(const DRAMChannelState& s)
    :state_(s)
{
    for (size_t i = 0; i < TOT_BANKS; i++)
    {
        size_t ba = fast_mod<DRAM_BANKS>(i),
               bg = fast_mod<DRAM_BANKGROUPS>(i >> numeric_traits<DRAM_BANKS>::log2),
               ra = fast_mod<DRAM_RANKS>(i >> numeric_traits<DRAM_BANKGROUPS*DRAM_BANKS>::log2);
        per_bank_queues_[i].bank_p_ = &s.at(ra).at(bg).at(ba);
    }
}

DRAMCommand
CommandScheduler::select_command()
{
    DRAMCommand cmd;
    for (size_t i = 0; i < per_bank_queues_.size(); i++) {
        auto& q = per_bank_queues_[next_cmd_queue_idx_];
        fast_increment_and_mod_inplace<TOT_BANKS>(next_cmd_queue_idx_);
    
        cmd = q.select_command(state_);
        if (!cmd_is_invalid(cmd.type))
            break;
    }
    return cmd;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
