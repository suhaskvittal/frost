/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_CHANNEL_h
#define DRAM_CHANNEL_h

#include "constants.h"

#include "dram/command.h"
#include "dram/enums.h"
#include "dram/scheduler.h"
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

using write_counts_array_type = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;

void dram_update_write_distribution_stats(const write_counts_array_type&, double& s_std, uint32_t& s_diff);

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

    uint32_t s_tot_read_latency_ =0;
    uint32_t s_tot_write_latency_ =0;

    uint32_t s_num_drains_ =0;
    uint32_t s_tot_read_occu_at_drain_ =0;
    uint32_t s_tot_write_occu_at_drain_ =0;

    BankUsageStats s_bank_usage_;
    /*
     * BELOW STATS ARE ONLY UPDATED AND PRINTED IF `DRAM_TRACK_ADVANCED_STATS` IS DEFINED.
     *  these are stats that are computationally intensive to compute, and thus can be disabled.
     * */
    using wrw_stat_type = std::array<uint32_t, 4>;

    wrw_stat_type s_num_seq_{};
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
    using write_drain_array_type = std::array<ssize_t, DRAM_TOT_BANKS_PER_CHANNEL>;
    /* 
     * Custom IO implementation
     * */
    in_queue_type read_queue_;
    in_queue_type write_queue_;
    pending_type pending_reads_;
    pending_type pending_writes_;

    active_buffer_type active_buffer_;
    /*
     * `writes_to_drain_per_bank_`: number of writes that can be issued (max) by a bank. Only used if
     *      `DRAM_WRITE_POLICY` is `SYNC`
     *  `tot_writes_to_drain_`: number of writes to issue from `write_queue_`
     *  `writes_issued_per_bank_`: purely for stats -- this is the writes issued from each bank in actuality.
     * */
    write_drain_array_type writes_to_drain_per_bank_{};
    size_t tot_writes_to_drain_ =0;
    bool in_write_mode_ =false;
    bool in_transition_ =false;
    write_counts_array_type writes_issued_per_bank_{};

    size_t starting_bank_idx_for_ready_cmd_ =0;

    DRAMChannelState  state_{};
    /*
     * Variables for tracking WR+W sequences:
     * */
    enum class WRWSequenceState { NEED_WRITE, NEED_READ, IN_READS };

    WRWSequenceState wrw_seq_state_ =WRWSequenceState::NEED_WRITE;
    uint64_t         wrw_first_write_cycle_ =0;
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

    void try_switch_to_write_mode(void);
    void issue_next_command(void);
    cmd_output_type select_ready_command(void);

    void update_wrw_state(const DRAMCommand&);
    /*
     * Directs the logger to only log write-read+-write sequences. Manages
     * and updates the state of the logger in this mode.
     * */
    void log_write_read_write_sequence(const DRAMCommand&);

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

#endif  // DRAM_CHANNEL_h
