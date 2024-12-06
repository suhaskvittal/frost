/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_h
#define DRAM_h

#include "dram/channel.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * This class is merely a simple wrapper for managing multiple DRAM
 * channels.
 * */
class DRAM
{
public:
    /*
     * IO mimics `IOBus` but really only implements `add_incoming`.
     * */
    struct IO
    {
        DRAM* dram;

        inline bool add_incoming(Transaction t)
        {
            size_t ch = dram_channel(t.address);
            dram->channels_[ch]->io_->outgoing_queue_.emplace(t, GL_CYCLE+100);
//          return dram->channels_[ch]->io_->add_incoming(t);
            return true;
        }

        IO(DRAM* d)
            :dram(d)
        {}
    };

    using io_ptr = std::unique_ptr<IO>;
    using channel_ptr = std::unique_ptr<DRAMChannel>;
    using channel_array_t = std::array<channel_ptr, DRAM_CHANNELS>;

    io_ptr io_;
    channel_array_t channels_;

    const double freq_ghz_;
private:
    double leap_ =0.0;

    const double clock_scale_;
public:
    DRAM(double cpu_freq_ghz, double freq_ghz);

    void tick(void);
    void print_stats(std::ostream&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_h
