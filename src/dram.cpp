/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "dram.h"

#include "globals.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t GL_DRAM_CYCLE = 0;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAM::DRAM(double cpu_freq_ghz, double freq_ghz, std::string dram_type)
    :io_(this),
    freq_ghz_(freq_ghz),
    clock_scale_(cpu_freq_ghz/freq_ghz - 1.0)
{
    for (size_t i = 0; i < DRAM_CHANNELS; i++)
        channels_[i] = channel_ptr(new DRAMChannel(freq_ghz, dram_type));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAM::tick()
{
    for (channel_ptr& ch : channels_) {
        auto& q = ch->io_->outgoing_queue_;
        while (!q.empty()) {
            io_->outgoing_queue_.push(q.top());
            q.pop();
        }
        if (leap_ < 1.0)
            ch->tick();
    }

    if (leap_ >= 1.0) {
        leap -= 1.0;
    } else {
        ++GL_DRAM_CYCLE;
        leap += clock_scale_;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define CREATE_VEC_STAT(stat)\
    VecStat<uint64_t,DRAM_CHANNELS> vec_stat;\
    for (size_t i = 0; i < DRAM_CHANNELS; i++) {\
        vec_stat[i] = channels_[i]->s_stat_;\
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
    CREATE_VEC_STAT(row_buffer_hits)

    VecStat<double, DRAM_CHANNELS> rbhr;
    for (size_t i = 0; i < DRAM_CHANNELS; i++)
        rbhr[i] = mean(vec_row_buffer_hits[i], vec_reads[i]+vec_writes[i]);

    print_vecstat(out, "DRAM", "NUM_READS", vec_reads);
    print_vecstat(out, "DRAM", "NUM_WRITES", vec_writes);
    print_vecstat(out, "DRAM", "NUM_PRECHARGE", vec_precharges);
    print_vecstat(out, "DRAM", "NUM_ACTIVATE", vec_activates);
    print_vecstat(out, "DRAM", "NUM_REFRESH", vec_refreshes);
    print_vecstat(out, "DRAM", "NUM_PREDEMAND", vec_pre_demand);
    print_vecstat(out, "DRAM", "ROW_BUFFER_HITS", vec_row_buffer_hits);
    print_vecstat(out, "DRAM", "ROW_BUFFER_HIT_RATE", rbhr);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
