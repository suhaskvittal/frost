/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#ifndef CACHE_h
#define CACHE_h

#include "util/numerics.h"

#include <array>
#include <cstdint>
#include <optional>
#include <random>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Defined in `globals.h`
 * */
extern uint64_t GL_CYCLE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class CacheReplPolicy { LRU, RAND, SRRIP, PERFECT };

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr uint8_t SRRIP_MAX = 7;

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
    uint8_t  rrpv;

    CacheEntry(void) =default;
    CacheEntry(uint64_t addr, size_t num_refs)
        :valid(true),
        address(addr),
        timestamp(GL_CYCLE),
        rrpv(num_refs > 1 ? SRRIP_MAX : 1)
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
    
    cset_array_t csets_{};
    std::mt19937_64 rng_{0};
public:
    using fill_result_t = std::optional<CacheEntry>;

    Cache(void) =default;

    bool probe(uint64_t);
    bool mark_dirty(uint64_t);
    /*
     * `num_refs` here corresponds to the number of MSHR/instruction references
     * at the time of install. Necessary for SRRIP, for example.
     * */
    fill_result_t fill(uint64_t, size_t num_refs);
    fill_result_t fill(entry_t&&);

    void invalidate(uint64_t);
    /*
     * Counts number of elements in cache meeting criteria. If `get_occupancy(void)` is
     * used, then this just counts the number of valid elements in the cache.
     * */
    template <class PRED>
    size_t get_occupancy(const PRED&);
    size_t get_occupancy(void);
    /*
     * Returns number of entries in the cache.
     * */
    inline size_t size(void)
    {
        return WAYS*SETS;
    }
private:
    typename cset_t::iterator find_victim(cset_t&);
    /*
     * Update replacement metadata for the entry.
     * */
    void update(entry_t&);

    inline cset_t& get_set(uint64_t x)
    {
        return csets_.at(fast_mod<SETS>(x));
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_h
