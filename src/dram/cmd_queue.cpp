/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#include "dram/cmd_queue.h"
#include "util/numerics.h"

#include <limits>
#include <numeric>

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

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommand
CommandScheduler::select_command()
{
    DRAMCommand cmd;
#if defined(DRAM_ENABLE_BG_WRITE_SYNC)
    if (bg_write_mode_)
        return bg_sync_select_write();
#endif
    for (size_t i = 0; i < per_bank_queues_.size(); i++)
    {
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

DRAMCommand
CommandScheduler::bg_sync_select_write()
{
    DRAMCommand cmd;
    for (size_t i = 0; i < bg_write_queues_.size(); i++)
    {
        auto& q = bg_write_queues_[bg_drain_idx_];
        fast_increment_and_mod_inplace<TOT_BANKGROUPS>(bg_drain_idx_);
        if (q.size() == 0)
        {
            if (bg_num_writes_ > DRAM_BANKGROUPS)
            {
                bg_write_mode_ = false;
                break;
            } 
            else
                continue;
        }

        cmd = q.select_command(state_);
        if (!cmd_is_invalid(cmd.type))
        {
            if (cmd_is_write(cmd.type))
                ++bg_num_writes_;
            break;
        }
    }
    return cmd;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
