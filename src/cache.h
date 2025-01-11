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
/*
 * Forward declarations for friend classes and functions:
 * */
template <class CACHE_TYPE> class VirtualWriteQueue;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <
    size_t SETS,
    size_t WAYS,
    CacheReplPolicy POL>
class Cache 
{
protected:
    using cset_type       = std::array<CacheEntry, WAYS>;
    using cset_array_type = std::array<cset_type, SETS>;
    
    cset_array_type csets_{};
    std::mt19937_64 rng_{0};
public:
    using find_result_type = std::tuple<cset_type*, typename cset_type::iterator>;
    using fill_result_type = std::optional<CacheEntry>;
    using multi_fill_result_type = std::tuple<fill_result_type, fill_result_type>;
    // Next line fill result also has the LRU position of the second line if it is dirty
    using next_line_fill_result_type = std::tuple<fill_result_type, fill_result_type, size_t>;

    Cache(void) =default;
    /*
     * Searches for the given line. Does not update any metadata. This is
     * like peeking into the cache.
     * */
    find_result_type find(uint64_t);

    virtual bool probe(uint64_t, bool write=false);
    virtual bool mark(uint64_t, bool as_dirty);
    /*
     * `num_refs` here corresponds to the number of MSHR/instruction references
     * at the time of install. Necessary for SRRIP, for example.
     *
     * `fill_with_eager_writeback` and other functions that return `multi_fill_result_type`
     * return a victim as well as any entries that should be written back. The caller
     * can do whatever they want with these entries, but keep in mind that the
     * cache has not evicted them. Furthermore, these entries are not references. If the
     * caller wants to modify the cache, they must call the appropriate function to do so.
     * */
    virtual fill_result_type fill(uint64_t, size_t num_refs);
    virtual multi_fill_result_type fill_with_eager_writeback(uint64_t, size_t);
    virtual next_line_fill_result_type fill_with_next_line_writeback(uint64_t, size_t);

    virtual void invalidate(uint64_t);
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
    inline constexpr size_t num_ways(void) const { return WAYS; }
    inline constexpr size_t num_sets(void) const { return SETS; }
    inline constexpr size_t size(void) const { return WAYS*SETS; }

    inline size_t get_set_index(uint64_t x) const { return fast_mod<SETS>(x); }
protected:
    virtual typename cset_type::iterator find_victim(cset_type&);
    /*
     * Update replacement metadata for the entry.
     * */
    virtual void update(CacheEntry&);
    /*
     * Gets way in the given LRU position.
     * */
    typename cset_type::iterator get_way_in_lru_pos(cset_type&);

    inline cset_type& get_set(uint64_t x) { return csets_.at(get_set_index(x)); }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_h
