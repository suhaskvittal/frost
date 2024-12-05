/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef CORE_h
#define CORE_h

#include "memsys.h"

#include "core/instruction.h"
#include "trace/reader.h"

#include <array>
#include <memory>
#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

using iptr_t = std::unique_ptr<Instruction>;

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

class Core
{
public:
    uint64_t finished_inst_num_ =0;
    bool done_ =false;

    const uint8_t coreid_;
private:
    using l1i_ptr = std::unique_ptr<L1ICache>;
    using l1d_ptr = std::unique_ptr<L1DCache>;
    using l2_ptr = std::unique_ptr<L2Cache>;
    
    using latch_t = std::array<Latch, CORE_FETCH_WIDTH>;
    using rob_t = std::deque<iptr_t>;
    /*
     * Caches: we will tie them together in the constructor.
     * */
    l1i_ptr L1I_;
    l1d_ptr L1D_;
    l2_ptr  L2_;
    /*
     * Pipeline latches: we model a simple decoupled frontend with three stages:
     *  IFtr: instruction fetch, translate ip
     *  IFmem: instruction fetch, access L1
     *  DISP: dispatch, install instruction into ROB
     * From there, each instruction will be on its own. We only model delays due
     * to memory accesses.
     * */
    latch_t la_iftr_ifmem_;
    latch_t la_ifmem_disp_;

    rob_t rob_;
    /*
     * Trace management:
     * */
    std::string trace_file_;
    TraceReader trace_reader_;
    /*
     * For stats, we don't want to collect any results once `checkpoint_stats` is
     * called. `stats_stream_` holds a checkpoint of the stats at the call time and
     * is dumped in `print_stats`.
     * */
    std::stringstream stats_stream_;
public:
    Core(uint8_t coreid, std::string trace_file);

    void tick(void);

    void checkpoint_stats(void);
    void print_stats(std::ostream&);
private:
    void iftr(size_t fwid);
    void ifmem(size_t fwid);
    void disp(size_t fwid);

    void operate_rob(void);

    void operate_caches(void);
    /*
     * This function needs access to `L2_`, which is private.
     * */
    friend void drain_llc_outgoing_queue(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Generic function handles drains from `c`. This is provided as a template function
 * due to the common pattern used below.
 * */
template <class CACHE_TYPE, class DRAIN_CALLBACK>
void drain_cache_outgoing_queue(cache_ptr<CACHE_TYPE>& c, DRAIN_CALLBACK handle_drain)
{
    // Need to make sure queue is drained at the appropriate time (hence the second check).
    auto& out_queue = c->io_->outgoing_queue_;
    while (!out_queue.empty()
            && GL_CYCLE >= std::get<1>(out_queue.top().cycle_done))
    {
        Transaction& t = std::get<0>(out_queue.top());
        handle_drain(t);
        out_queue.pop();
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void drain_llc_outgoing_queue(void);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CORE_h
