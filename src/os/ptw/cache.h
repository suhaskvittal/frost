/*
 *  author: Suhas Vittal
 *  date:   8 December 2024
 * */

#ifndef OS_PTW_CACHE_h
#define OS_PTW_CACHE_h

#include <cstdint>
#include <cstddef>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * This is a custom cache class because we don't need all the bells
 * and whistles of `cache.tpp`. Also, the page walker caches cannot
 * be templated as they depend on `PT_LEVELS`.
 *
 * Page table walker cache (PTWC) maps bits in the vpn
 * to the physical address of page table directory address.
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct PTWCacheEntry
{
    bool valid =false;
    uint64_t bits;
    uint64_t timestamp =0;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Pared-down cache implementation. Assumes an LRU replacement policy.
 * */
class PTWCache
{
public:
    uint64_t s_accesses_ =0;
    uint64_t s_misses_ =0;
    
    const size_t num_sets_;
    const size_t num_ways_;
private:
    using cset_t =       std::vector<PTWCacheEntry>;
    using cset_array_t = std::vector<cset_t>;

    cset_array_t csets_;
public:
    PTWCache(size_t num_sets, size_t num_ways);

    bool probe_and_fill_on_miss(uint64_t);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

using ptwc_ptr = std::unique_ptr<PTWCache>;
using ptwc_array_t = std::array<ptwc_ptr, PT_LEVELS-1>;

size_t ptwc_get_initial_directory_level(ptwc_array_t&, uint64_t vpn);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif   // OS_PTW_CACHE_h
