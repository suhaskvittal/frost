/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_CHANNEL_h
#define DRAM_CHANNEL_h

#include "constants.h"
#include "dram/bank.h"
#include "dram/command.h"
#include "transaction.h"

#include <array>
#include <deque>
#include <optional>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Defined in `io_bus.h`
 * */
class IOBus;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMPagePolicy { OPEN, CLOSE };

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMChannel
{
public:
    using io_ptr = std::unique_ptr<IOBus>;

    uint64_t s_reads_ =0;
    uint64_t s_writes_ =0;
    uint64_t s_precharges_ =0;
    uint64_t s_activates_ =0;
    uint64_t s_refreshes_ =0;
    uint64_t s_pre_demand_ =0;

    uint64_t s_row_buffer_hits_ =0;
    /**/
    uint64_t s_tot_read_after_read_latency_  =0;
    uint64_t s_tot_read_after_write_latency_ =0;
    uint64_t s_num_read_after_read_  =0;
    uint64_t s_num_read_after_write_ =0;

    io_ptr io_;

    const double freq_ghz_;
private:
    constexpr static size_t TOT_BANKS = DRAM_RANKS*DRAM_BANKGROUPS*DRAM_BANKS;

    using bank_array_t = std::array<DRAMBank, TOT_BANKS>;
    using constraint_t = std::array<uint64_t, 2>;
    using faw_t = std::deque<uint64_t>;

    bank_array_t banks_; 
    size_t next_bank_with_cmd_ =0;
    /*
     * Channel-level timing constraints
     * First element is different bankgroup timing, second is same bankgroup.
     * */
    constraint_t act_ok_cycle_{{0,0}}; 
    constraint_t rd_ok_cycle_{{0,0}};
    constraint_t wr_ok_cycle_{{0,0}};
    faw_t faw_;

    size_t last_bankgroup_ =0;
    /*
     * Refresh management.
     * */
    uint64_t next_ref_cycle_;
    uint64_t ref_done_cycle_ =0;

    bool last_cmd_was_read_ =true;
public:

    DRAMChannel(double freq_ghz);
    ~DRAMChannel(void);
    
    void tick_mc(void);
    void tick_dram(void);
private:
    using sel_cmd_t = std::optional<DRAMCommand>;
    /*
     * `schedule_next_cmd` moves a command from `io_`'s read/write queues to
     * some bank's command queues.
     *
     * `issue_next_cmd` selects a command from a bank's command queue via
     * FRFCFS. If no command can be issued or no command exists, then this
     * does nothing.
     * */
    void schedule_next_cmd(void);
    void issue_next_cmd(void);

    bool cmd_is_issuable(const DRAMCommand&);
    void update_timing(const DRAMCommand&);

    sel_cmd_t select_next_command(void);

    DRAMBank& get_bank(uint64_t);

    void bank_update_act(DRAMBank&, uint64_t row);
    void bank_update_cas(DRAMBank&, bool is_read, bool autopre);
    void bank_update_pre(DRAMBank&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CHANNEL_h
