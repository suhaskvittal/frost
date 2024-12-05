/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#ifndef CACHE_h
#define CACHE_h

#include "globals.h"

#include "util/numerics.h"

#include <array>
#include <cstdint>
#include <optional>
#include <random>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class CacheReplPolicy { LRU, RAND };

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct CacheEntry
{
    bool valid =false;
    bool dirty =false;
    uint64_t address;
    /*
     * Replacement policy data:
     * */
    uint64_t timestamp;

    CacheEntry(void) =default;
    CacheEntry(uint64_t addr)
        :valid(true),
        address(addr),
        timestamp(GL_CYCLE)
    {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t SETS, size_t WAYS, CacheReplPolicy POL>
class Cache 
{
private:
    using entry_t      = CacheEntry;
    using cset_t       = std::array<entry_t, WAYS>;
    using cset_array_t = std::array<cset_t, SETS>;
    
    cset_array_t csets_;
    std::mt19937_64 rng_{0};
public:
    using fill_result_t = std::optional<CacheEntry>;

    Cache(void) =default;

    bool probe(uint64_t);
    bool mark_dirty(uint64_t);

    fill_result_t fill(uint64_t);
    fill_result_t fill(entry_t&&);

    void invalidate(uint64_t);
private:
    typename cset_t::iterator find_victim(cset_t&);

    inline size_t get_index(uint64_t x)
    {
        return numeric_traits<SETS>::mod(x);
    }

    inline cset_t& get_set(uint64_t x)
    {
        return csets_.at(get_index(x));
    }

    inline void update(CacheEntry& e)
    {
        e.timestamp = GL_CYCLE;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_h
