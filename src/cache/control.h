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

    const std::string cache_name_;
private:
    using mshr_t = std::unordered_multimap<uint64_t, MSHREntry>;

    next_ptr& next_;
    mshr_t    mshr_;
public:
    CacheControl(std::string cache_name, next_ptr&);

    void tick(void);
    void mark_load_as_done(uint64_t address);
private:
    void next_access(void);
    void handle_hit(const Transaction&);
    void handle_miss(const Transaction&, bool write_miss=false);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "control.tpp"

#endif  // CACHE_CONTROL_h
