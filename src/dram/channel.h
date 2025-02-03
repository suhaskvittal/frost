/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_CHANNEL_h
#define DRAM_CHANNEL_h

#include "constants.h"

#include "dram/command.h"
#include "dram/enums.h"
#include "dram/state.h"
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

struct RWQueueEntry
{
    Transaction trans;
    bool is_row_buffer_hit;

    uint64_t cycle_entered_queue;

    RWQueueEntry(Transaction t)
        :trans(t),
        cycle_entered_queue(GL_DRAM_CYCLE)
    {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct SchedulerState
{
    using issue_prio_array = std::array<int8_t, DRAM_TOT_BANKS_PER_CHANNEL>;

    issue_prio_array highest_priority;

    SchedulerState(void)
    {
        highest_priority.fill(std::numeric_limits<int8_t>::lowest());
    }

    inline void update_priority(size_t bank_idx, int8_t p)
    {
        int8_t& cp = highest_priority[bank_idx];
        cp = std::max(cp, p);
    }
};

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

using write_counts_array = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;

void dram_update_write_distribution_stats(const write_counts_array&, double& s_std, uint32_t& s_diff);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMChannel
{
public:
    using in_queue_type = std::vector<RWQueueEntry>;
    using pending_type = std::unordered_set<uint64_t>;

    out_queue_type outgoing_queue_;

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
    uint32_t s_tot_read_occu_at_drain_ =0;
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
    const size_t low_watermark_;
    const size_t high_watermark_;
private:
    using active_buffer_type = std::unordered_set<size_t>;
    using write_drain_array = std::array<ssize_t, DRAM_TOT_BANKS_PER_CHANNEL>;
    /* 
     * Custom IO implementation
     * */
    in_queue_type read_queue_;
    in_queue_type write_queue_;
    pending_type pending_reads_;
    pending_type pending_writes_;

    active_buffer_type active_buffer_;
    /*
     * `write_budget_per_bank_`: number of writes that can be issued (max) by a bank. Only used if
     *      `DRAM_WRITE_POLICY` is `SYNC`
     *  `writes_issued_per_bank_`: purely for stats -- this is the writes issued from each bank in actuality.
     *
     *  Other:
     *      `in_write_mode_`: this is true if the DRAM is issuing writes
     *      `in_transition_`: this is true if the DRAM is finishing up any straggling ACTs
     *      `drain_start_cycle_`: for computing `s_tot_drain_latency_` -- the start cycle of a write drain
     * */
    write_drain_array write_budget_per_bank_{};
    bool in_write_mode_ =false;
    bool in_transition_ =false;
    write_counts_array writes_issued_per_bank_{};
    uint64_t drain_start_cycle_ =0;

    size_t starting_bank_idx_for_ready_cmd_ =0;

    DRAMChannelState  state_{};
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
public:
    DRAMChannel(size_t channel_id, double freq_ghz);

    void tick(void);

    bool add_incoming(Transaction);
    bool deadlock_find_inst(const inst_ptr) const;

    inline size_t read_queue_size(void) const { return read_queue_.size(); }
    inline size_t write_queue_size(void) const { return write_queue_.size(); }
private:
    using cmd_output_type = std::tuple<DRAMCommand, std::optional<RWQueueEntry>>;
    using rw_const_iterator = in_queue_type::const_iterator;
    /*
     * `try_to_switch_to_write_mode` tries to set `in_transition_` if in read mode and sets
     * writing structures accordingly (i.e., `tot_writes_to_drain_`)
     *
     * `issue_next_command` calls `select_ready_command` to get a command. If it is not invalid,
     * it is issued and the timing state is updated.
     *
     * `select_ready_command` finds a ready command and tries to schedule from each bank in a round
     * robin manner
     * */
    void try_switch_to_write_mode(void);
    void issue_next_command(void);
    cmd_output_type select_ready_command(void);

    void update_modal_stats_post_transition(void);
    /*
     * Here is the scheduler implementation:
     *  `scheduler_allow_demand_precharge` is effectively the bulk of the scheduling algorithm,
     *      as it determines what operations get priority.
     *  `scheduler_get_cas_command` determines whether or not to issue an auto-precharge.l
     * */
    bool scheduler_allow_demand_precharge(
            in_queue_type::const_iterator,
            const in_queue_type&,
            const SchedulerState&,
            const DRAMBankState&);
    DRAMCommandType scheduler_get_cas_command(
            in_queue_type::const_iterator,
            const in_queue_type&,
            const SchedulerState&,
            const DRAMBankState&);
    /*
     *
     * */
    inline DRAMBankState& get_bank_ref_from_idx(size_t ii)
    {
        size_t i = fast_mod<DRAM_BANKS>(ii),
               j = fast_mod<DRAM_BANKGROUPS>(ii >> numeric_traits<DRAM_BANKS>::log2),
               k = fast_mod<DRAM_RANKS>(ii >> numeric_traits<DRAM_BANKS*DRAM_BANKGROUPS>::log2);
        return state_[k][j][i];
    }

    inline const DRAMBankState& get_bank_const_ref_from_idx(size_t ii) const
    {
        size_t i = fast_mod<DRAM_BANKS>(ii),
               j = fast_mod<DRAM_BANKGROUPS>(ii >> numeric_traits<DRAM_BANKS>::log2),
               k = fast_mod<DRAM_RANKS>(ii >> numeric_traits<DRAM_BANKS*DRAM_BANKGROUPS>::log2);
        return state_.at(k).at(j).at(i);
    }

    friend class DRAM;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cache/other_impl/all.h"

/*
 * Here are just some auxilliary functions for updating the LLC.
 * */

template <class CACHE_TYPE>
void update_cache_post_write_drain(std::unique_ptr<CACHE_TYPE>& c, size_t channel_id, size_t writes_drained_per_bank)
{
    if constexpr (is_bank_balanced_cache<typename CACHE_TYPE::parent_type>::value)
    {
        c->handle_write_drain(channel_id, writes_drained_per_bank);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CHANNEL_h
