/*
 *  author: Suhas Vittal
 *  date:   5 January 2025
 * */

#ifndef DRAM_ALT_IMPL_CHANNEL_h
#define DRAM_ALT_IMPL_CHANNEL_h
/*
 * This is an alternative implementation of a `DRAMChannel`, where
 * commands are issued directly from the RW queues.
 *
 * Has same public facing implementation.
 * */

#include "dram/enums.h"

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

    uint64_t s_num_drains_ =0;
    /*
     * BELOW STATS ARE ONLY UPDATED AND PRINTED IF `DRAM_TRACK_ADVANCED_STATS` IS DEFINED.
     *  these are stats that are computationally intensive to compute, and thus can be disabled.
     * */

    const double freq_ghz_;
    const size_t channel_id_;
private:
    in_queue_t read_queue_;
    in_queue_t write_queue_;
    pending_t pending_reads_;
    pending_t pending_writes_;
    size_t writes_to_drain_ =0;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_ALT_IMPL_CHANNEL_h
