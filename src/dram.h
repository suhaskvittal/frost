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
     * IO mimics `IOBus` but really only implements `add_incoming` and `outgoing_queue_`.
     * */
    struct IO
    {
        IOBus::out_queue_t outgoing_queue_;
        DRAM* dram;

        inline bool add_incoming(Transaction t)
        {
            size_t ch = dram_get_channel(t.address);
            return dram->channels_[ch]->io_->add_incoming(t);
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
    DRAM(double freq_ghz, std::string dram_type)

    void tick(void);
    void print_stats(std::ostream&);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_h
