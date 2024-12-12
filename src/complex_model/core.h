/* author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef COMPLEX_MODEL_CORE_h
#define COMPLEX_MODEL_CORE_h

#include "constants.h"
#include "instruction.h"

#include <array>
#include <deque>
#include <memory>
#include <iosfwd>
#include <string>
#include <sstream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Latch
{
    bool valid =false;
    bool stalled =false;
    iptr_t inst;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * These are defined in `memsys.h`.
 * */
struct L1ICache;
struct L1DCache;
struct L2Cache;
/*
 * Defined in `trace/reader.h`
 * */
class TraceReader;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class Core
{
public:
    uint64_t finished_inst_num_ =0;
    bool done_ =false;

    uint64_t s_iftr_stalls_ =0;
    uint64_t s_ifmem_stalls_ =0;
    uint64_t s_disp_stalls_ =0;

    const uint8_t coreid_;
private:
    using l1i_ptr = std::unique_ptr<L1ICache>;
    using l1d_ptr = std::unique_ptr<L1DCache>;
    using l2_ptr = std::unique_ptr<L2Cache>;
    
    using latch_t = std::array<Latch, CORE_FETCH_WIDTH>;
    using ftb_t = std::deque<iptr_t>;
    using rob_t = std::deque<iptr_t>;

    using tracereader_t = std::unique_ptr<TraceReader>;
    /*
     * Caches: we will tie them together in the constructor.
     * */
    l1i_ptr L1I_;
    l1d_ptr L1D_;
    l2_ptr  L2_;
    /*
     * Pipeline latches: we model a simple decoupled frontend:
     *  IFbp: instruction fetch, branch prediction
     *  IFtr: instruction fetch, translate ip
     *  IFmem: instruction fetch, access L1
     *
     * After IFmem, the instruction is placed into the fetch target buffer (`ftb_`).
     * Once an instruction's load is finished, then it is removed from the FTB
     * during the DISP stage and placed into the ROB.
     *
     * From there, each instruction will be on its own. We only model delays due
     * to memory accesses.
     * */
    latch_t la_ifbp_iftr_;
    latch_t la_iftr_ifmem_;
    ftb_t ftb_;
    rob_t rob_;
    /*
     * Trace management:
     * */
    std::string   trace_file_;
    tracereader_t trace_reader_;
    uint64_t      inst_num_ =0;
    /*
     * For stats, we don't want to collect any results once `checkpoint_stats` is
     * called. `stats_stream_` holds a checkpoint of the stats at the call time and
     * is dumped in `print_stats`.
     * */
    std::stringstream stats_stream_;
public:
    Core(uint8_t coreid, std::string trace_file);
    ~Core(void);
    /*
     * Warmup will just take the next instruction and update the caches.
     * */
    void tick_warmup(void);
    void tick(void);

    void checkpoint_stats(void);
    void print_stats(std::ostream&);
private:
    void ifbp(size_t fwid);
    void iftr(size_t fwid);
    void ifmem(size_t fwid);
    void disp(size_t fwid);
    void operate_rob(void);
    void operate_caches(void);

    iptr_t next_inst(bool warmup=false);
    /*
     * This function needs access to `L2_`, which is private.
     * */
    friend void drain_llc_outgoing_queue(void);
    /*
     * OS needs access to `L1D_` to initialize the page table walker.
     * */
    friend class OS;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void inst_dtlb_access(iptr_t&, uint8_t coreid);
void inst_dtlb_done(iptr_t&, uint64_t vpn, uint64_t pfn);
void inst_dcache_access(iptr_t&, uint8_t coreid, std::unique_ptr<L1DCache>&);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CORE_h
