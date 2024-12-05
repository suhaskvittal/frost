/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "dram.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAM::DRAM(double freq_ghz, std::string dram_type)
    :io_(this),
    freq_ghz_(freq_ghz),
    clock_scale_(4.0/freq_ghz - 1.0)
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

    if (leap_ >= 1.0)
        leap -= 1.0;
    else 
        leap += clock_scale_;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
