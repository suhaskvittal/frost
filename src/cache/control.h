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

#include "cache/other_impl/virtual_write_queue.h"

#include <memory>
#include <random>
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
 * Different cache writeback policies:
 * */
enum class CacheWBMode
{
    FORCED,                 // standard writeback -- only writeback when dirty line is evicted
    EAGER,                  // FORCED + writeback whenever dirty line reaches LRU position.
    NEXT_LINE,              // FORCED + writeback if a dirty line's neighboring line is in the same set,
                            // also needs `INDEX_OFFSET` of cache to be 1 or higher.
    VIRTUAL_WRITE_QUEUE     // Performs writebacks according to the virtual write queue implementation.
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
 *  DEFINED BY USER:
 *      (1) `WRITEBACK_MODE` -- by default, this should be `FORCED`
 *      (2) `LAZY_EARLY_WRITEBACK` -- if using any other policy than `FORCED`, then if a early writeback
 *                                      cannot be issued, the line remains dirty and writeback does not
 *                                      take up an MSHR.
 *
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

    using stat_type = VecStat<uint64_t, NUM_THREADS>;

    cache_ptr  cache_;
    io_ptr     io_;
    
    stat_type s_accesses_{};
    stat_type s_misses_{};
    stat_type s_tot_penalty_{};
    stat_type s_num_penalty_{};
    stat_type s_invalidates_{};
    stat_type s_write_alloc_{};

    uint64_t s_writebacks_ =0;
    uint64_t s_dirty_victim_adj_lines_ =0;
    uint64_t s_dirty_victim_adj_lines_also_dirty_ =0;

    uint64_t s_eager_writebacks_ =0;
    uint64_t s_scheduled_writebacks_ =0;

    uint64_t s_tot_next_line_lru_pos_ =0;
    uint64_t s_tot_next_lines_ =0;

    const std::string cache_name_;
private:
    using mshr_type = std::unordered_multimap<uint64_t, MSHREntry>;
    using wb_queue_type = std::deque<uint64_t>;

    next_ptr& next_;
    /*
     * MSHR space is split between `mshr_` and `writeback_queue_`. Note that
     * in a real system, pending writebacks would be stored in the MSHR.
     * */
    mshr_type     mshr_;
    wb_queue_type writeback_queue_;
    /*
     * Specific implementations that are nonstandard:
     * */
    constexpr static size_t EAGER_QUEUE_SIZE = 32;

    using eager_queue_type = std::deque<uint64_t>;
    using vwq_ptr = std::unique_ptr<VirtualWriteQueue<CACHE>>;

    eager_queue_type eager_queue_;
    /*
     * `vwq_` operates as a wrapper for some of `cache_`'s functionality.
     * */
    vwq_ptr vwq_ =nullptr;

    std::mt19937_64 rng_{0};
public:
    CacheControl(std::string cache_name, next_ptr&);

    void warmup_access(uint64_t, bool write);

    void tick(void);
    void mark_load_as_done(uint64_t address);
    /*
     * Only use `is_dirty` if installing to an `INVALIDATE_ON_HIT` cache.
     * */
    void demand_fill(uint64_t address, size_t refcnt, bool is_dirty=false);
    /*
     * Searches for an instruction in this cache. If it is found, a message
     * is printed to `stderr` and this function returns true.
     * */
    bool deadlock_find_inst(const inst_ptr);

    inline size_t curr_mshr_size(void) const { return mshr_.size() + writeback_queue_.size(); }
private:
    void next_access(void);
    void handle_hit(const Transaction&);
    void handle_miss(const Transaction&, bool write_miss=false);

    void handle_eager_writeback(CacheEntry&);

    bool do_writeback(uint64_t addr);
    /*
     * Since `probe` and `mark` may require different functionality (i.e.,
     * if `VIRTUAL_WRITE_QUEUE` is enabled), we have a simple wrapper here.
     * */
    bool cache_probe(uint64_t address, bool write=false);
    bool cache_mark(uint64_t address, bool dirty);
    /*
     * Virtual write queue implementation:
     * */ 
    void vwq_schedule_writebacks(void);
    void vwq_schedule_writebacks_on_fill(uint64_t address);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Generic function handles drains from `c`. This is provided as a template function
 * due to the common pattern used below.
 * */
template <class CACHE_TYPE, class DRAIN_CALLBACK>
void drain_cache_outgoing_queue(std::unique_ptr<CACHE_TYPE>&, const DRAIN_CALLBACK&);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * We don't define this function here as it depends on core model. Should be
 * defined in the respective `core.cpp` file.
 * */
void drain_llc_outgoing_queue(void);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "control.tpp"

#endif  // CACHE_CONTROL_h
