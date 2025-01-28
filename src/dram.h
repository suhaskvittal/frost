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

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Defined in `transaction.h`
 * */
class Transaction;

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

        IO(DRAM*);
        bool can_accept(uint64_t, TransactionType);
        bool add_incoming(Transaction);
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

    void warmup_access(uint64_t, bool) {}

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
