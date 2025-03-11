/*
 *  author: Suhas Vittal
 *  date:   10 March 2025
 * */

#ifndef CACHE_OTHER_IMPL_W_CACHE2_h
#define CACHE_OTHER_IMPL_W_CACHE2_h

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

using util_array = std::vector<ssize_t>;

inline ssize_t
wcache_compute_utility(util_array::const_iterator begin, util_array::const_iterator end, size_t w)
{
    return std::reduce(begin+w, end, static_cast<ssize_t>(0));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, class NEXT_TYPE>
class WCache2 : public __TEMPLATE_PARENT__
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
    using way_counter_array = util_array;

    channel_data_array channels_{};

    way_counter_array read_hits_;
    way_counter_array write_hits_;

    size_t max_virtual_buffer_size_ =0;

    const size_t sampled_sets_;
    const size_t set_modulus_;
    const size_t set_modulus_ilog2_;

    using __TEMPLATE_PARENT__::csets_;
public:
    WCache2(std::string, typename __TEMPLATE_PARENT__::next_ptr&);

    void tick(void) override;
    void channel_request_demand_writeback(size_t channel_id);
private:
    using typename __TEMPLATE_PARENT__::way_iterator;

    bool probe(const Transaction&) override;

    void child_handle_probe_hit(cset_type&, cset_type::iterator, const Transaction&) override;
    void child_handle_mark_dirty_hit(cset_type&, cset_type::iterator, const Transaction&) override;

    way_iterator find_victim(size_t set_index, cset_type&, const Transaction&) override;
    way_iterator repl_lru_w(size_t, cset_type&, const Transaction&);
    way_iterator repl_rrip_w(size_t, cset_type&, const Transaction&);

    void enqueue_writeback(Transaction) override;
    /*
     * Useful inlines:
     * */
    inline bool has_writeback_priority(uint64_t addr) const
    {
        size_t c = dram_channel(addr),
               b = dram_bank_idx(addr);

        return !channels_.at(c).writeback_done.test(b);
    }

    inline bool is_sampled_set(size_t idx) const
    {
        return fast_mod(idx, set_modulus_) == (idx >> set_modulus_ilog2_);
    }
    /*
     * Constexpr functions for initialization:
     * */
    constexpr static size_t num_hit_counters(void)
    {
        if constexpr (repl_is_rrip_based(IMPL::REPL))
            return RRIP_MAX;
        else
            return IMPL::NUM_WAYS;
    }

    using __TEMPLATE_PARENT__::mshr_has_space;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "w_cache2.tpp"

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif   // CACHE_OTHER_IMPL_W_CACHE2_h
