/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_h
#define DRAM_h

#if defined(USE_DRAMSIM3)
#include "dramsim3_wrapper.h"
#else

#include "dram/channel.h"
#include "transaction.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * This class is merely a simple wrapper for managing multiple DRAM
 * channels.
 * */
class DRAM
{
public:
    using channel_ptr = std::unique_ptr<DRAMChannel>;
    using channel_array_t = std::array<channel_ptr, DRAM_CHANNELS>;

    channel_array_t channels_;
    const double freq_ghz_;
private:
    double leap_ =0.0;

    const double clock_scale_;
public:
    DRAM(double cpu_freq_ghz, double freq_ghz);

    void warmup_access(uint64_t, bool) {}

    bool can_accept(uint64_t, TransactionType);
    bool add_incoming(Transaction);

    void tick(void);
    void print_stats(std::ostream&);

    inline bool deadlock_find_inst(const inst_ptr inst) const
    {
        return std::any_of(channels_.begin(), channels_.end(),
                    [inst] (const auto& ch)
                    {
                        return ch->deadlock_find_inst(inst);
                    });
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
#endif  // DRAM_h
