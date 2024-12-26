/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_CHANNEL_h
#define DRAM_CHANNEL_h

#include "constants.h"
#include "dram/command.h"
#include "dram/cmd_queue.h"
#include "dram/state.h"
#include "io_bus.h"
#include "transaction.h"

#include <array>
#include <deque>
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

    const double freq_ghz_;
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
public:
    DRAMChannel(double freq_ghz);
    
    void tick_mc(void);
    void tick_dram(void);

    bool add_incoming(Transaction);
private:
    void issue_next_cmd(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CHANNEL_h
