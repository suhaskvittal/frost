/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef CACHE_CONTROL_h
#define CACHE_CONTROL_h

#include "cache.h"

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
        :trans(t),
        is_for_write_allocate(is_write),
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
 *  Each setting determines how `CacheControl` operates `CACHE`
 *  and `NEXT_CONTROL`.
 * */
template <class IMPL, class CACHE, class NEXT_CONTROL>
class CacheControl
{
public:
    using io_ptr = std::shared_ptr<IOBus>;
    using cache_ptr = std::unique_ptr<CACHE>;
    using next_ptr = std::unique_ptr<NEXT_CONTROL>;

    cache_ptr  cache_;
    io_ptr     io_;
    
    uint64_t s_tot_penalty_ =0;
    uint64_t s_num_penalty_ =0;
    uint64_t s_invalidate_ =0;
    uint64_t s_write_alloc_ =0;
    uint64_t s_fills_ =0;
private:
    using mshr_t = std::unordered_multimap<uint64_t, MSHREntry>;

    next_ptr& next_;
    mshr_t    mshr_;
public:
    CacheControl(next_ptr&);

    void mark_load_as_done(uint64_t address);
private:
    void handle_hit(const Transaction&);
    void handle_miss(const Transaction&, bool write_miss=false);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////


#endif  // CACHE_CONTROL_h
