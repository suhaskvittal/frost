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

void
CommandScheduler::bg_sync_init(size_t start)
{
    bg_drain_idx_ = start;
    bg_write_mode_ = true;
    bg_writes_done_ = (1L << TOT_BANKGROUPS)-1;

    ++s_num_bg_sync_drains_;
    s_mean_bg_sync_queue_size_ += std::transform_reduce(bg_write_queues_.begin(), bg_write_queues_.end(), 
                                    0.0,
                                    std::plus<double>{},
                                    [] (const auto& x)
                                    { 
                                        return static_cast<double>(x.size());
                                    }) / static_cast<double>(DRAM_BANKGROUPS);
    const auto& [min_it, max_it] = std::minmax_element(bg_write_queues_.begin(), bg_write_queues_.end(),
                                    [] (const auto& x, const auto& y)
                                    {
                                        return x.size() < y.size();
                                    });
    s_tot_bg_sync_drain_spread_ += max_it->size() - min_it->size();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommand
CommandScheduler::bg_sync_select_write()
{
    DRAMCommand cmd;
    for (size_t i = 0; i < bg_write_queues_.size(); i++)
    {
        size_t ii = bg_drain_idx_;
        auto& q = bg_write_queues_[ii];
        fast_increment_and_mod_inplace<TOT_BANKGROUPS>(bg_drain_idx_);

        if (q.size() == 0)
        {
            bg_writes_done_ &= ~(1L << ii);
            if (bg_writes_done_ == 0)
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
            {
                bg_writes_done_ &= ~(1L << ii);
                ++s_tot_bg_sync_writes_;
            }
            break;
        }
    }
    return cmd;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
