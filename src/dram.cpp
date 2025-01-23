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

DRAM::IO::IO(DRAM* d)
    :dram(d)
{}

bool
DRAM::IO::can_accept(uint64_t address, TransactionType type)
{
    bool is_write = trans_is_write(type);
#if defined(DRAM_DROP_WRITES)
    if (is_write)
        return true;
#endif
    size_t i = dram_channel(address);
    if (is_write)
        return dram->channels_[i]->write_queue_size() < DRAM_WQ_SIZE;
    else
        return dram->channels_[i]->read_queue_size() < DRAM_RQ_SIZE;
}

bool
DRAM::IO::add_incoming(Transaction t)
{
#if defined(DRAM_DROP_WRITES)
    if (t.type == TransactionType::WRITE)
        return true;
#endif
    size_t i = dram_channel(t.address);
    return dram->channels_[i]->add_incoming(t);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAM::DRAM(double cpu_freq_ghz, double freq_ghz)
    :io_(new DRAM::IO(this)),
    freq_ghz_(freq_ghz),
    clock_scale_(cpu_freq_ghz/freq_ghz - 1.0)
{
    for (size_t i = 0; i < DRAM_CHANNELS; i++)
        channels_[i] = channel_ptr(new DRAMChannel(i, freq_ghz));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAM::tick()
{
    if (leap_ < 1.0)
    {
        for (channel_ptr& ch : channels_)
        {
            auto& q = ch->outgoing_queue_;
            if (q.count(GL_DRAM_CYCLE) > 0)
            {
                auto [begin, end] = q.equal_range(GL_DRAM_CYCLE);
                for (auto it = begin; it != end; it++)
                    GL_LLC->mark_load_as_done(it->second.address);
                q.erase(begin, end);
            }
            ch->tick();
        }
        ++GL_DRAM_CYCLE;
        leap_ += clock_scale_;
    }
    else
        leap_ -= 1.0;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define CREATE_VEC_STAT(stat)\
    VecStat<uint64_t,DRAM_CHANNELS> stat;\
    for (size_t i = 0; i < DRAM_CHANNELS; i++) {\
        stat[i] = channels_[i]->s_##stat##_;\
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
    CREATE_VEC_STAT(num_drains)
    CREATE_VEC_STAT(tot_read_occu_at_drain)
    CREATE_VEC_STAT(tot_write_occu_at_drain)

    VecStat<double, DRAM_CHANNELS> rd_rbhr = vec_elwise_mean(read_row_hits, reads),
                                   wr_rbhr = vec_elwise_mean(write_row_hits, writes),
                                   read_latency = vec_elwise_mean(tot_read_latency, reads),
                                   write_latency = vec_elwise_mean(tot_write_latency, writes),
                                   mean_read_occu_at_drain = vec_elwise_mean(tot_read_occu_at_drain, num_drains),
                                   mean_write_occu_at_drain = vec_elwise_mean(tot_write_occu_at_drain, num_drains),
                                   writes_per_drain = vec_elwise_mean(writes, num_drains);
    // Get bank usage stats:
    VecStat<double, DRAM_CHANNELS> bank_read_std,
                                    bank_write_std;
    for (size_t i = 0; i < DRAM_CHANNELS; i++)
    {
        bank_read_std[i] = vec_std(channels_[i]->s_bank_usage_.reads);
        bank_write_std[i] = vec_std(channels_[i]->s_bank_usage_.writes);
    }
    
    out << BAR << "\n";

    print_vecstat(out, "DRAM", "NUM_READS", reads);
    print_vecstat(out, "DRAM", "NUM_WRITES", writes);
    print_vecstat(out, "DRAM", "NUM_PRECHARGE", precharges);
    print_vecstat(out, "DRAM", "NUM_ACTIVATE", activates);
    print_vecstat(out, "DRAM", "NUM_REFRESH", refreshes);
    print_vecstat(out, "DRAM", "NUM_PREDEMAND", pre_demand);
    print_vecstat(out, "DRAM", "READ_ROW_BUFFER_HIT_RATE", rd_rbhr, VecAccMode::HMEAN);
    print_vecstat(out, "DRAM", "WRITE_ROW_BUFFER_HIT_RATE", wr_rbhr, VecAccMode::HMEAN);
    print_vecstat(out, "DRAM", "READ_LATENCY", read_latency, VecAccMode::GMEAN);
    print_vecstat(out, "DRAM", "WRITE_LATENCY", write_latency, VecAccMode::GMEAN);
    
    out << "\n";

    print_vecstat(out, "DRAM", "BANK_READ_STANDARD_DEVIATION", bank_read_std, VecAccMode::GMEAN);
    print_vecstat(out, "DRAM", "BANK_WRITE_STANDARD_DEVIATION", bank_write_std, VecAccMode::GMEAN);

    /*
    for (size_t i = 0; i < DRAM_TOT_BANKS_PER_CHANNEL; i++)
    {
        VecStat<uint64_t, DRAM_CHANNELS> writes;
        for (size_t j = 0; j < DRAM_CHANNELS; j++)
            writes[j] = channels_[j]->s_bank_usage_.writes[i];
        print_vecstat(out, "DRAM", "WRITES_TO_BANK_" + std::to_string(i), writes);
    }
    */
    
    out << "\n";

    print_vecstat(out, "DRAM", "NUM_WRITE_DRAINS", num_drains);
    print_vecstat(out, "DRAM", "WRITES_PER_DRAIN", writes_per_drain, VecAccMode::HMEAN);
    print_vecstat(out, "DRAM", "MEAN_READ_OCCUPANCY_AT_DRAIN", mean_read_occu_at_drain, VecAccMode::GMEAN);
    print_vecstat(out, "DRAM", "MEAN_WRITE_OCCUPANCY_AT_DRAIN", mean_write_occu_at_drain, VecAccMode::GMEAN);

#if defined(DRAM_TRACK_ADVANCED_STATS)
    out << "\n";

    CREATE_VEC_STAT(tot_write_queue_std)
    CREATE_VEC_STAT(tot_write_queue_minmax_diff)
    CREATE_VEC_STAT(tot_write_issue_std)
    CREATE_VEC_STAT(tot_write_issue_minmax_diff)

    VecStat<double, DRAM_CHANNELS> mean_write_queue_std = vec_elwise_mean(tot_write_queue_std, num_drains),
                                   mean_write_queue_minmax_diff = vec_elwise_mean(tot_write_queue_minmax_diff, num_drains),
                                   mean_write_issue_std = vec_elwise_mean(tot_write_issue_std, num_drains),
                                   mean_write_issue_minmax_diff = vec_elwise_mean(tot_write_issue_minmax_diff, num_drains);

    for (size_t i = 0; i < 4; i++)
    {
        uint64_t max_cyc = 1L << (i+8);
        std::array<uint64_t,2> vec{channels_[0]->s_num_seq_[i], channels_[1]->s_num_seq_[i]};
        print_vecstat(out, "DRAM", "NUM_WR+W_SEQ_LE_" + std::to_string(max_cyc), vec);
    }

    print_vecstat(out, "DRAM", "MEAN_WRITE_QUEUE_STANDARD_DEVIATION", mean_write_queue_std, VecAccMode::GMEAN);
    print_vecstat(out, "DRAM", "MEAN_WRITE_QUEUE_MINMAX_DIFFERENCE", mean_write_queue_minmax_diff, VecAccMode::GMEAN);
    print_vecstat(out, "DRAM", "MEAN_WRITE_ISSUE_STANDARD_DEVIATION", mean_write_issue_std, VecAccMode::GMEAN);
    print_vecstat(out, "DRAM", "MEAN_WRITE_ISSUE_MINMAX_DIFFERENCE", mean_write_issue_minmax_diff, VecAccMode::GMEAN);
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
