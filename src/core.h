/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef CORE_h
#define CORE_h

#include "memsys_decl.h"

#include "core/instruction.h"
#include "trace/reader.h"

#include <array>
#include <memory>

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

    uint64_t s_llc_misses_ =0;
private:
    using l1i_ptr = cache_ptr<L1ICache>;
    using l1d_ptr = cache_ptr<L1DCache>;
    using l2_ptr = cache_ptr<L2Cache>;

    using tracereader_t = std::unique_ptr<TraceReader>;
    
    using latch_t = std::array<Latch, CORE_FETCH_WIDTH>;
    using rob_t = std::array<iptr_t, CORE_ROB_SIZE>;
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
    /*
     * ROB: implement this as a fixed-width queue with a head and size variable.
     * */
    rob_t rob_;
    size_t rob_head_ =0;
    size_t rob_size_ =0;
    /*
     * Trace management:
     * */
    std::string   trace_file_;
    tracereader_t trace_reader_;
public:
    Core(std::string trace_file);

    void tick(void);
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

using drain_callback_t = std::function<void(const Transaction&)>;
/*
 * Generic function handles drains from `c`. This is provided as a template function
 * due to the common pattern used below.
 * */
template <class CACHE_TYPE>
void drain_cache_outgoing_queue(cache_ptr<CACHE_TYPE>& c, drain_callback_t handle_drain)
{
    // Need to make sure queue is drained at the appropriate time (hence the second check).
    while (!c->outgoing_queue_.empty()
            && GL_CYCLE >= std::get<1>(c->outgoing_queue_.top().cycle_done))
    {
        Transaction& t = std::get<0>(c->outgoing_queue_.top());
        handle_drain(t);
        c->outgoing_queue_.pop();
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void drain_llc_outgoing_queue(void);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CORE_h
