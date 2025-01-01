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
#include <tuple>

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

template <
    size_t SETS,
    size_t WAYS,
    CacheReplPolicy POL,
    // Optionals:
    size_t INDEX_OFFSET=0>
class Cache 
{
private:
    using cset_t       = std::array<CacheEntry, WAYS>;
    using cset_array_t = std::array<cset_t, SETS>;
    
    cset_array_t csets_{};
    std::mt19937_64 rng_{0};
public:
    using find_result_t = std::tuple<cset_t*, typename cset_t::iterator>;
    using fill_result_t = std::optional<CacheEntry>;
    using multi_fill_result_t = std::tuple<fill_result_t, fill_result_t>;

    Cache(void) =default;
    /*
     * Searches for the given line. Does not update any metadata. This is
     * like peeking into the cache.
     * */
    find_result_t find(uint64_t);

    bool probe(uint64_t, bool write=false);
    bool mark_dirty(uint64_t);
    bool mark_clean(uint64_t);
    /*
     * `num_refs` here corresponds to the number of MSHR/instruction references
     * at the time of install. Necessary for SRRIP, for example.
     *
     * `fill_with_eager_writeback` and other functions that return `multi_fill_result_t`
     * return a victim as well as any entries that should be written back. The caller
     * can do whatever they want with these entries, but keep in mind that the
     * cache has not evicted them. Furthermore, these entries are not references. If the
     * caller wants to modify the cache, they must call the appropriate function to do so.
     * */
    fill_result_t       fill(uint64_t, size_t num_refs);
    multi_fill_result_t fill_with_eager_writeback(uint64_t, size_t);
    multi_fill_result_t fill_with_next_line_writeback(uint64_t, size_t);

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
    void update(CacheEntry&);
    /*
     * Retrieves the respective entry at the `k`-th LRU position.
     * */
    CacheEntry& get_way_in_lru_pos(cset_t&, size_t k=0);

    inline cset_t& get_set(uint64_t x)
    {
        return csets_.at(fast_mod<SETS>(x >> INDEX_OFFSET));
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_h
