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

enum class CacheReplPolicy { LRU, RAND, SRRIP, PERFECT, DRRIP };

constexpr uint8_t RRIP_MAX = 15;

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

    bool likely_dead =false;

    CacheEntry(void) =default;
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
public:
    uint64_t s_dueling_pol1_installs_ =0;
    uint64_t s_dueling_pol2_installs_ =0;
protected:
    enum class SetDuelingRole { LEADER_1 =1, LEADER_2 =-1, FOLLOWER =0 };

    using psel_type = int16_t;
    using cset_type       = std::array<CacheEntry, WAYS>;
    using cset_array_type = std::array<cset_type, SETS>;

    constexpr static size_t LEADER_SETS = 64;
    constexpr static size_t PSEL_WIDTH = 11;
    constexpr static psel_type PSEL_MAX = (1 << PSEL_WIDTH) - 1;
    constexpr static psel_type PSEL_MIN = 0;
    constexpr static psel_type PSEL_THRESHOLD = (1 << (PSEL_WIDTH-1));
    constexpr static psel_type PSEL_DEFAULT = PSEL_THRESHOLD-1;

    constexpr static size_t BIMODAL_CTR_MAX = 32;
    
    cset_array_type csets_{};
    psel_type psel_ =PSEL_DEFAULT;
    size_t bimodal_ctr_ =0;

    std::mt19937_64 rng_{0};
public:
    using find_result_type = std::tuple<cset_type*, typename cset_type::iterator>;
    using fill_result_type = std::optional<CacheEntry>;
    using eager_fill_result_type = std::tuple<fill_result_type, fill_result_type>;

    Cache(void) =default;
    /*
     * Searches for the given line. Does not update any metadata. This is
     * like peeking into the cache.
     * */
    find_result_type find(uint64_t);

    inline bool get_dirty_bit(uint64_t address)
    {
        auto [s_p, it] = find(address);
        return it->dirty;
    }

    virtual bool probe(uint64_t, bool write=false);
    virtual bool mark(uint64_t, bool as_dirty);
    /*
     * `num_refs` here corresponds to the number of MSHR/instruction references
     * at the time of install. Necessary for SRRIP, for example.
     *
     * `fill_with_eager_writeback` and other functions that return `eager_fill_result_type`
     * return a victim as well as any entries that should be written back. The caller
     * can do whatever they want with these entries, but keep in mind that the
     * cache has not evicted them. Furthermore, these entries are not references. If the
     * caller wants to modify the cache, they must call the appropriate function to do so.
     * */
    virtual fill_result_type 
        fill(uint64_t, size_t num_refs, bool mark_dirty=false);
    virtual eager_fill_result_type
        fill_with_eager_writeback(uint64_t, size_t, bool mark_dirty=false);
    /*
     * These functions probe the associated cache set and checks if there are any victims that meet
     * the given criteria:
     *  `invalid_victim` -- any entry with `valid == false`
     *  `noncritical_victim` -- any entry with `valid == false || likely_dead == true`
     * */
    bool fill_will_replace_invalid_victim(uint64_t address) const;
    bool fill_will_replace_noncritical_victim(uint64_t address) const;

    virtual void invalidate(uint64_t);

    virtual void mark_likely_dead(uint64_t);
    virtual void mark_likely_alive(uint64_t);
    /*
     * Counts number of elements in cache meeting criteria. If `get_occupancy(void)` is
     * used, then this just counts the number of valid elements in the cache.
     * */
    template <class PRED>
    size_t get_occupancy(const PRED&);
    size_t get_occupancy(void);

    inline static constexpr size_t num_ways(void) { return WAYS; }
    inline static constexpr size_t num_sets(void) { return SETS; }
    inline static constexpr size_t size(void) { return WAYS*SETS; }
    inline static constexpr CacheReplPolicy repl(void) { return POL; }

    inline static constexpr bool uses_set_dueling(void) 
    {
        return POL == CacheReplPolicy::DRRIP;
    }

    inline static size_t _get_set_index(uint64_t x)
    {
        return fast_mod<SETS>(x);
    }
protected:
    virtual typename cset_type::iterator find_victim(cset_type&);

    typename cset_type::iterator lru(cset_type&);
    typename cset_type::iterator rand(cset_type&);
    typename cset_type::iterator rrip(cset_type&);
    /*
     * Update replacement metadata for the entry.
     * */
    virtual void update_entry(CacheEntry&);
    virtual void init_entry(CacheEntry&, uint64_t address, size_t num_refs, bool mark_dirty);
    /*
     * Gets way in the given LRU position.
     * */
    typename cset_type::iterator get_likely_dead_line(cset_type&);
    typename cset_type::iterator get_way_in_lru_pos(cset_type&);

    virtual SetDuelingRole get_set_role(size_t idx) const;
    virtual void update_psel(size_t idx);

    inline virtual size_t get_set_index(uint64_t x) const
    {
        return _get_set_index(x);
    }

    inline cset_type& get_set(uint64_t x)
    {
        return csets_[get_set_index(x)];
    }

    inline const cset_type& get_const_set(uint64_t x) const
    {
        return csets_.at(get_set_index(x));
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_h
