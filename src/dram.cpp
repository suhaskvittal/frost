/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "constants.h"
#include "globals.h"
#include "memsys.h"

#include "dram.h"
#include "dram/address.h"
#include "dram/channel.h"
#include "io_bus.h"
#include "util/stats.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t GL_DRAM_CYCLE = 0;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAM::DRAM(double cpu_freq_ghz, double freq_ghz)
    :io_(new DRAM::IO(this)),
    freq_ghz_(freq_ghz),
    clock_scale_(cpu_freq_ghz/freq_ghz - 1.0)
{
    for (size_t i = 0; i < DRAM_CHANNELS; i++)
        channels_[i] = channel_ptr(new DRAMChannel(freq_ghz));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAM::tick()
{
    for (channel_ptr& ch : channels_) {
        auto& q = ch->outgoing_queue_;
        while (!q.empty()) {
            const auto& [t, cycle_done] = q.top();
            if (GL_DRAM_CYCLE < cycle_done)
                break;
            GL_LLC->mark_load_as_done(t.address);
            q.pop();
        }
        if (leap_ < 1.0)
            ch->tick_dram();
        ch->tick_mc();
    }

    if (leap_ >= 1.0) {
        leap_ -= 1.0;
    } else {
        ++GL_DRAM_CYCLE;
        leap_ += clock_scale_;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define CREATE_VEC_STAT(stat)\
    VecStat<uint64_t,DRAM_CHANNELS> vec_##stat;\
    for (size_t i = 0; i < DRAM_CHANNELS; i++) {\
        vec_##stat[i] = channels_[i]->s_##stat##_;\
    }\

void
DRAM::print_stats(std::ostream& out)
{
    CREATE_VEC_STAT(reads)
    CREATE_VEC_STAT(writes)
    CREATE_VEC_STAT(precharges)
    CREATE_VEC_STAT(activates)
    CREATE_VEC_STAT(refreshes)
    CREATE_VEC_STAT(pre_demand)
    CREATE_VEC_STAT(read_row_hits)
    CREATE_VEC_STAT(write_row_hits)

    CREATE_VEC_STAT(tot_read_latency)
    CREATE_VEC_STAT(tot_write_latency)

    VecStat<double, DRAM_CHANNELS> rd_rbhr, wr_rbhr;
    VecStat<double, DRAM_CHANNELS> read_latency, write_latency;

    for (size_t i = 0; i < DRAM_CHANNELS; i++)
    {
        rd_rbhr[i] = mean(vec_read_row_hits[i], vec_reads[i]);
        wr_rbhr[i] = mean(vec_write_row_hits[i], vec_writes[i]);

        read_latency[i] = mean(vec_tot_read_latency[i], vec_reads[i]);
        write_latency[i] = mean(vec_tot_write_latency[i], vec_writes[i]);
    }

    out << BAR << "\n";

    print_vecstat(out, "DRAM", "NUM_READS", vec_reads);
    print_vecstat(out, "DRAM", "NUM_WRITES", vec_writes);
    print_vecstat(out, "DRAM", "NUM_PRECHARGE", vec_precharges);
    print_vecstat(out, "DRAM", "NUM_ACTIVATE", vec_activates);
    print_vecstat(out, "DRAM", "NUM_REFRESH", vec_refreshes);
    print_vecstat(out, "DRAM", "NUM_PREDEMAND", vec_pre_demand);

    print_vecstat(out, "DRAM", "READ_ROW_BUFFER_HIT_RATE", rd_rbhr, VecAccMode::HMEAN);
    print_vecstat(out, "DRAM", "WRITE_ROW_BUFFER_HIT_RATE", wr_rbhr, VecAccMode::HMEAN);

    print_vecstat(out, "DRAM", "READ_LATENCY", read_latency, VecAccMode::GMEAN);
    print_vecstat(out, "DRAM", "WRITE_LATENCY", write_latency, VecAccMode::GMEAN);
#ifdef DRAM_TRACK_ADVANCED_STATS
    CREATE_VEC_STAT(drain_bg_spread);
    CREATE_VEC_STAT(num_drains);

    VecStat<double, DRAM_CHANNELS> mean_drain_bg_spread;
    for (size_t i = 0; i < DRAM_CHANNELS; i++)
    {
        mean_drain_bg_spread[i] = mean(vec_drain_bg_spread[i], vec_num_drains[i]);
    }

    print_vecstat(out, "DRAM", "MEAN_BANKGROUP_DRAIN_SPREAD", mean_drain_bg_spread, VecAccMode::GMEAN);
    print_vecstat(out, "DRAM", "NUM_WRITE_DRAINS", vec_num_drains);
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
