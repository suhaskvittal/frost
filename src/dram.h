/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_h
#define DRAM_h

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Defined in `transaction.h`
 * */
class Transaction;
/*
 * Defined in `dram/channel.h`
 * */
class DRAMChannel;

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
    ~DRAM(void);

    void tick(void);
    void print_stats(std::ostream&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_h
