/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_CHANNEL_h
#define DRAM_CHANNEL_h

#include "constants.h"

#include "dram/command.h"
#include "dram/cmd_queue.h"
#include "dram/scheduler.h"
#include "dram/state.h"
#include "io_bus.h"
#include "transaction.h"

#include <array>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <optional>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMPagePolicy { OPEN, CLOSE };

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMChannel
{
public:
    using in_queue_t = IOBus::in_queue_t;
    using pending_t = IOBus::pending_t;
    using out_queue_t = IOBus::out_queue_t;

    out_queue_t outgoing_queue_;

    uint64_t s_reads_ =0;
    uint64_t s_writes_ =0;
    uint64_t s_precharges_ =0;
    uint64_t s_activates_ =0;
    uint64_t s_refreshes_ =0;

    uint64_t s_pre_demand_ =0;

    uint64_t s_read_row_hits_ =0;
    uint64_t s_write_row_hits_ =0;

    uint64_t s_tot_read_latency_ =0;
    uint64_t s_tot_write_latency_ =0;
    /*
     * BELOW STATS ARE ONLY UPDATED AND PRINTED IF `DRAM_TRACK_ADVANCED_STATS` IS DEFINED.
     *  these are stats that are computationally intensive to compute, and thus can be disabled.
     * */
    uint64_t s_tot_drain_bg_spread_ =0;
    uint64_t s_num_drains_ =0;

    const double freq_ghz_;
    const size_t channel_id_;
private:
    using cmd_sch_ptr = std::unique_ptr<CommandScheduler>;
    /* 
     * Custom IO implementation
     * */
    in_queue_t read_queue_;
    in_queue_t write_queue_;
    pending_t pending_reads_;
    pending_t pending_writes_;
    size_t writes_to_drain_ =0;

    DRAMChannelState  state_{};
    cmd_sch_ptr cmd_scheduler_;

    bool c128_tracking_writes_ =false;
    size_t c128_ctr_ =0;
    uint64_t c128_start_cycle_ =0;
    /*
     * All variables below are used if `DRAM_ENABLE_LOGGER` is defined.
     * 
     * `dram_logger_` is used to log information and write to a file. File is default: `dram_channel.<id>.log`.
     *
     * `tmp_logger_` is used to buffer log info. `tmp_logger_` is only used to write to `dram_logger_` if a
     * command is issued.
     *
     * `last_two_cmds_` tracks the last two commands. Useful for logging dependent on commands.
     * */
    enum class LoggerCmdState { NEED_WRITE, NEED_READ, IN_READS };

    std::ofstream     dram_logger_{};
    std::stringstream tmp_logger_local_;
    std::stringstream tmp_logger_global_;
    LoggerCmdState    logger_state_ =LoggerCmdState::NEED_WRITE;
    uint64_t          logger_first_write_cycle_ =0;
public:
    DRAMChannel(size_t channel_id, double freq_ghz);
    
    void tick_mc(void);
    void tick_dram(void);

    bool add_incoming(Transaction);
private:
    void issue_next_cmd(void);

    friend class DRAM;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CHANNEL_h
