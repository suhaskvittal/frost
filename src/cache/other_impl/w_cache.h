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

extern size_t OPT_WCACHE_BALANCE_BUFFER_SIZE;
extern size_t OPT_WCACHE_HATS_COUNT;

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
        using balance_buffer_array = std::array<std::deque<Transaction>, DRAM_TOT_BANKS_PER_CHANNEL>;

        bool                 in_write_mode =false;
        size_t               next_demand_idx =0;
        bank_bitvec_type     writeback_done{};
        balance_buffer_array balance_buffer;
    };

    using channel_data_array = std::array<channel_data_type, DRAM_CHANNELS>;
    using way_counter_array = std::vector<ssize_t>;
    using target_type = std::pair<size_t, size_t>;
    using target_array = std::vector<target_type>;

    constexpr static size_t SAMPLED_SETS = 64;
    constexpr static size_t SET_MODULUS = IMPL::NUM_SETS / SAMPLED_SETS;

    constexpr static size_t DLUP_WIDTH = 10;
    constexpr static int16_t DLUP_MIN = 0;
    constexpr static int16_t DLUP_MAX = (1 << DLUP_WIDTH)-1;
    constexpr static int16_t DLUP_INIT = 1 << (DLUP_WIDTH-1);
    constexpr static int16_t DLUP_MASK = 1 << (DLUP_WIDTH-1);
    /*
     * Structures for tracking channel state:
     * */
    channel_data_array channels_{};

    way_counter_array false_evict_counters_;
    size_t            max_fill_lookup_pos_;

    int16_t demand_lookup_util_pred_ =DLUP_INIT;

    using __TEMPLATE_PARENT__::csets_;
public:
    WCache(std::string, typename __TEMPLATE_PARENT__::next_ptr&);

    void tick(void) override;
    void channel_request_demand_writeback(size_t channel_id);
private:
    using typename __TEMPLATE_PARENT__::way_iterator;

    void child_handle_probe_hit(cset_type&, cset_type::iterator, const Transaction&) override;
    void child_handle_mark_dirty_hit(cset_type&, cset_type::iterator, const Transaction&) override;

    void init_entry(CacheEntry&, const Transaction&);

    way_iterator find_victim(size_t set_index, cset_type&, const Transaction&) override;
    way_iterator repl_lru_w(size_t, cset_type&, const Transaction&);
    way_iterator repl_rrip_w(size_t, cset_type&, const Transaction&);

    void enqueue_writeback(Transaction) override;
    /*
     * Useful inlines:
     * */
    inline size_t max_position_to_use_for_writeback_priority(size_t idx) const
    {
        return fast_mod(idx, SET_MODULUS) == 0 ? num_false_evict_counters() : max_fill_lookup_pos_;
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
        return fast_mod(idx, SET_MODULUS) == (idx >> ilog2(SET_MODULUS));
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
