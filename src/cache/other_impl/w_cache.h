/*
 *  author: Suhas Vittal
 *  date:   7 March 2024
 * */

#ifndef CACHE_OTHER_IMPL_W_CACHE_h
#define CACHE_OTHER_IMPL_W_CACHE_h

#include "cache.h"

#include <array>
#include <bitset>
#include <deque>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<IMPL,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * Set this to -1 to avoid fixing the lookup position:
 * */
extern int    OPT_WCACHE_FIXED_LOOKUP_POS;
extern size_t OPT_WCACHE_SAMPLED_SETS;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr inline void update_sel(int16_t& s, bool pos, int16_t min, int16_t max)
{
    s = pos ? (s+1) : (s-1);
    s = std::clamp(s, min, max);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, class NEXT_TYPE>
class WCache : public __TEMPLATE_PARENT__
{
public:
    using __TEMPLATE_PARENT__::s_writebacks_;
    using __TEMPLATE_PARENT__::s_eager_writebacks_;
private:
    struct channel_data_type
    {
        using bank_bitvec_type = std::bitset<DRAM_TOT_BANKS_PER_CHANNEL>;
        /*
         * Some of the data in here may be unused.
         * */
        bool in_write_mode =false;
        size_t next_demand_idx =0;
        bank_bitvec_type writeback_done{};
    };

    using channel_data_array = std::array<channel_data_type, DRAM_CHANNELS>;
    using way_counter_array = std::vector<int16_t>;
    using target_type = std::pair<size_t, size_t>;
    using target_array = std::vector<target_type>;

    constexpr static size_t SEL_WIDTH = 8;
    constexpr static int16_t SEL_MIN = 0;
    constexpr static int16_t SEL_MAX = (1 << SEL_WIDTH)-1;
    constexpr static int16_t SEL_INIT = 1 << (SEL_WIDTH-1);
    constexpr static int16_t SEL_THRESHOLD = 1 << (SEL_WIDTH-1);
    /*
     * Structures for tracking channel state:
     * */
    channel_data_array channels_{};

    way_counter_array false_evict_counters_;
    size_t            max_fill_lookup_pos_=4;

    const size_t sampled_sets_;
    const size_t set_modulus_;
    const size_t set_modulus_ilog2_;

    using __TEMPLATE_PARENT__::csets_;
public:
    WCache(std::string, typename __TEMPLATE_PARENT__::next_ptr&);

    void tick(void) override;

    inline void channel_write_mode_update(size_t channel_id, bool enter)
    {
        channels_[channel_id].in_write_mode = enter;
    }
private:
    using typename __TEMPLATE_PARENT__::way_iterator;

    void child_handle_probe_hit(cset_type&, cset_type::iterator, const Transaction&) override;
    void child_handle_mark_dirty_hit(cset_type&, cset_type::iterator, const Transaction&) override;

    void init_entry(CacheEntry&, const Transaction&);

    way_iterator find_victim(size_t set_index, cset_type&, const Transaction&) override;
    way_iterator repl_lru_w(size_t, cset_type&, const Transaction&);
    way_iterator repl_rrip_w(size_t, cset_type&, const Transaction&);

    void enqueue_writeback(Transaction) override;

    inline void handle_victim_from_sampled_set(cset_type::iterator v_it)
    {
        if (v_it->test_evict_pos >= 0)
            update_sel(false_evict_counters_[v_it->test_evict_pos], true, SEL_MIN, SEL_MAX);
    }
    /*
     * Useful inlines:
     * */
    inline size_t max_position_to_use_for_writeback_priority(size_t idx) const
    {
        if (OPT_WCACHE_FIXED_LOOKUP_POS < 0)
            return is_sampled_set(idx) ? num_false_evict_counters() : max_fill_lookup_pos_;
        else
            return OPT_WCACHE_FIXED_LOOKUP_POS;
    }

    inline bool any_banks_without_writebacks(void) const
    {
        return std::any_of(channels_.begin(), channels_.end(),
                        [] (const auto& c) { return c.writeback_done.count() < DRAM_TOT_BANKS_PER_CHANNEL; });
    }

    inline bool has_writeback_priority(uint64_t addr) const
    {
        size_t c = dram_channel(addr),
               b = dram_bank_idx(addr);

        return !channels_.at(c).writeback_done.test(b);
    }

    inline bool is_sampled_set(size_t idx) const
    {
        return OPT_WCACHE_FIXED_LOOKUP_POS < 0 && fast_mod(idx, set_modulus_) == (idx >> set_modulus_ilog2_);
    }
    /*
     * Constexpr static inlines:
     * */
    constexpr static inline size_t num_false_evict_counters(void)
    {
        if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP || IMPL::REPL == CacheReplPolicy::DRRIP)
            return RRIP_MAX+1;
        else
            return IMPL::NUM_WAYS;
    }

    constexpr static inline size_t initial_max_pos(void)
    {
        if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP || IMPL::REPL == CacheReplPolicy::DRRIP)
            return 2;
        else
            return 4;
    }

    using __TEMPLATE_PARENT__::mshr_has_space;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * Comparison logic between two lines `x` and `y` given a few flags:
 * */
bool repl_cmp_w(bool x_is_older, bool x_is_dirty, bool y_is_dirty, bool x_has_prio, bool y_has_prio);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "w_cache.tpp"

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_W_CACHE_h
