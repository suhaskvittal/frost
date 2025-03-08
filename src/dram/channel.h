/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_CHANNEL_h
#define DRAM_CHANNEL_h

#include "constants.h"

#include "cache/other_impl/type_traits.h"
#include "dram/command.h"
#include "dram/enums.h"
#include "dram/alt_scheduler.h"
#include "dram/scheduler.h"
#include "dram/state.h"
#include "dram/stats.h"
#include "transaction.h"
#include "util/numerics.h"
#include "util/out_queue.h"

#include <array>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <optional>
#include <unordered_set>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct BankUsageStats
{
    template <class T>
    using vec_stat_type = std::array<T, DRAM_TOT_BANKS_PER_CHANNEL>;

    using counts_type = vec_stat_type<uint32_t>;

    counts_type reads{};
    counts_type writes{};
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMChannel
{
public:
    out_queue_type outgoing_queue_;

    uint32_t s_read_requests_ =0;
    uint32_t s_write_requests_ =0;

    uint32_t s_reads_ =0;
    uint32_t s_writes_ =0;
    uint32_t s_precharges_ =0;
    uint32_t s_activates_ =0;
    uint32_t s_refreshes_ =0;

    uint32_t s_pre_demand_ =0;

    uint32_t s_read_row_hits_ =0;
    uint32_t s_write_row_hits_ =0;

    uint64_t s_tot_read_latency_ =0;
    uint64_t s_tot_write_latency_ =0;

    uint32_t s_num_drains_ =0;
    uint32_t s_num_forced_drains_ =0;
    uint32_t s_tot_read_occu_at_drain_ =0;
    uint32_t s_tot_read_occu_post_drain_ =0;
    uint32_t s_tot_write_occu_at_drain_ =0;
    uint64_t s_tot_drain_latency_ =0;

    BankUsageStats s_bank_usage_;
    /*
     * BELOW STATS ARE ONLY UPDATED AND PRINTED IF `DRAM_TRACK_ADVANCED_STATS` IS DEFINED.
     *  these are stats that are computationally intensive to compute, and thus can be disabled.
     * */
    double s_tot_write_queue_std_ =0.0;
    double s_tot_write_issue_std_ =0.0;
    uint32_t s_tot_write_queue_minmax_diff_ =0;
    uint32_t s_tot_write_issue_minmax_diff_ =0;

    const double freq_ghz_;
    const size_t channel_id_;

    const size_t virtual_write_queue_watermark_;
private:
#if defined(DRAM_USE_ALT_SCHEDULER)
    using scheduler_impl = AlternateDRAMScheduler;
#else
    using scheduler_impl = DRAMScheduler;
#endif

    using scheduler_ptr = std::unique_ptr<scheduler_impl>;
    using core_cycle_array = std::array<uint64_t, NUM_THREADS>;

    scheduler_ptr scheduler_;
    DRAMChannelState  state_{};
    /*
     * Used to compute write drain stats:
     * */ 
    uint64_t drain_start_cycle_;
    write_counts_array writes_issued_per_bank_;

    core_cycle_array last_read_issued_for_core_cycle_{};
    /*
     * All variables below are used if `DRAM_ENABLE_LOGGER` is defined.
     * 
     * `dram_logger_` is used to log information and write to a file. File is default: `dram_channel.<id>.log`.
     *
     * `tmp_logger_` is used to buffer log info. `tmp_logger_` is only used to write to `dram_logger_` if a
     * command is issued.
     * */
    std::ofstream     dram_logger_{};
    std::stringstream tmp_logger_;

    bool     logger_in_write_mode_ =false;
    uint64_t logger_last_cas_cycle_ =0;
public:
    DRAMChannel(size_t channel_id, double freq_ghz);

    void tick(void);
    bool deadlock_find_inst(const inst_ptr) const;
    void update_modal_stats_post_transition(void);
    /*
     * Public inlines:
     * */
    inline bool can_accept(const Transaction& trans) const
    {
        return scheduler_->can_accept(trans);
    }

    inline bool add_incoming(Transaction trans)
    {
        bool success = scheduler_->add_incoming(trans);

        if (success)
        {
            if (trans.is_read())
                ++s_read_requests_;
            else
                ++s_write_requests_;
        }

        return success;
    }
    
    inline bool precharge_do_counter_update(void)
    {
        return false;
    }
private:
    void issue_next_command(void);
    /*
     * Auxilliary functions for updating the LLC. This needs to be a template so functions that are
     * not defined in `Cache` but in a different class can be used.
     * */
    template <class CACHE_TYPE>
    void cache_toggle_write_mode(std::unique_ptr<CACHE_TYPE>& c, bool w)
    {
        if constexpr (cache_type_traits::is_mcp_cache<typename CACHE_TYPE::parent_type>::value)
            c->toggle_write_mode(channel_id_, w);
    }

    template <class CACHE_TYPE>
    void cache_send_demand_writeback_request(std::unique_ptr<CACHE_TYPE>& c)
    {
        if constexpr (cache_type_traits::is_virtual_write_queue<typename CACHE_TYPE::parent_type>::value)
            c->channel_request_demand_writeback(channel_id_);

        if constexpr (cache_type_traits::is_mcp_cache<typename CACHE_TYPE::parent_type>::value)
            c->channel_request_demand_writeback(channel_id_);

        if constexpr (cache_type_traits::is_w_cache<typename CACHE_TYPE::parent_type>::value)
            c->channel_request_demand_writeback(channel_id_);
    }

    friend class DRAM;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CHANNEL_h
