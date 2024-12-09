/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef CACHE_CONTROL_h
#define CACHE_CONTROL_h

#include "constants.h"

#include "cache.h"
#include "io_bus.h"
#include "transaction.h"
#include "util/stats.h"

#include <memory>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct MSHREntry
{
    bool is_fired =false;
    bool is_for_write_allocate;
    Transaction trans;

    uint64_t cycle_fired;

    MSHREntry(const Transaction& t, bool is_write=false)
        :is_for_write_allocate(is_write),
        trans(t),
        cycle_fired(GL_CYCLE)
    {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * `IMPL` operates as a traits class that defines
 *      (1) `NUM_MSHR`,
 *      (2) `WRITE_ALLOCATE` (whether or not to handle write misses)
 *      (3) `INVALIDATE_ON_HIT`
 *      (4) `NEXT_IS_INVALIDATE_ON_HIT`
 *      (5) `NUM_RW_PORTS`
 *      (6) `CACHE_LATENCY`
 *  Each setting determines how `CacheControl` operates `CACHE`
 *  and `NEXT_CONTROL`.
 * */
template <class IMPL, class CACHE, class NEXT_CONTROL>
class CacheControl
{
public:
    using io_ptr = std::unique_ptr<IOBus>;
    using cache_ptr = std::unique_ptr<CACHE>;
    using next_ptr = std::unique_ptr<NEXT_CONTROL>;

    using stat_t = VecStat<uint64_t, NUM_THREADS>;

    cache_ptr  cache_;
    io_ptr     io_;
    
    stat_t s_accesses_{};
    stat_t s_misses_{};
    stat_t s_tot_penalty_{};
    stat_t s_num_penalty_{};
    stat_t s_invalidates_{};
    stat_t s_write_alloc_{};

    uint64_t s_writebacks_ =0;

    const std::string cache_name_;
private:
    using mshr_t = std::unordered_multimap<uint64_t, MSHREntry>;
    using wb_queue_t = std::deque<uint64_t>;

    next_ptr& next_;
    /*
     * MSHR space is split between `mshr_` and `writeback_queue_`. Note that
     * in a real system, pending writebacks would be stored in the MSHR.
     * */
    mshr_t     mshr_;
    wb_queue_t writeback_queue_;
public:
    CacheControl(std::string cache_name, next_ptr&);

    void warmup_access(uint64_t, bool write);

    void tick(void);
    void mark_load_as_done(uint64_t address);
    /*
     * Only use `is_dirty` if installing to an `INVALIDATE_ON_HIT` cache.
     * */
    void demand_fill(uint64_t address, bool is_dirty=false);
    /*
     * Searches for an instruction in this cache. If it is found, a message
     * is printed to `stderr` and this function returns true.
     * */
    bool deadlock_find_inst(const iptr_t& inst);
private:
    void next_access(void);
    void handle_hit(const Transaction&);
    void handle_miss(const Transaction&, bool write_miss=false);

    inline bool do_writeback(uint64_t addr)
    {
        Transaction t(0, nullptr, TransactionType::WRITE, addr);
        if (next_->io_->add_incoming(t))
            return true;
        else
            return false;
    }

    inline size_t curr_mshr_size(void)
    {
        return mshr_.size() + writeback_queue_.size();
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "control.tpp"

#endif  // CACHE_CONTROL_h
