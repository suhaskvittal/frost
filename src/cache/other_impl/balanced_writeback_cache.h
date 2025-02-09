/*
 *  author: Suhas Vittal
 *  date:   3 February 2025
 * */

#ifndef CACHE_OTHER_IMPL_BALANCED_WRITEBACK_CACHE_h
#define CACHE_OTHER_IMPL_BALANCED_WRITEBACK_CACHE_h

#include "constants.h"
#include "cache.h"
#include "dram/address.h"

#include <algorithm>
#include <array>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<IMPL,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, class NEXT_TYPE>
class BalancedWritebackCache : public __TEMPLATE_PARENT__
{
public:
protected:
    using wb_local_counter_array = std::array<std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>, DRAM_CHANNELS>;
    using wb_global_counter_array = std::array<size_t, DRAM_CHANNELS>;

    constexpr static size_t MAX_WRITE_COUNTER = DRAM_WQ_SIZE / DRAM_TOT_BANKS_PER_CHANNEL;
    /*
     * These structures are used to buffer writes and determine when to use the modified replacement policy
     * (i.e., see `lru_mod` and `rrip_mod` below)
     * */
    wb_local_counter_array balance_counters_{};
    wb_global_counter_array total_writebacks_{};

    using __TEMPLATE_PARENT__::next_;
    using __TEMPLATE_PARENT__::dbp_;
    using __TEMPLATE_PARENT__::mshr_;
public:
    using __TEMPLATE_PARENT__::Cache;
    using typename __TEMPLATE_PARENT__::way_iterator;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    using typename __TEMPLATE_PARENT__::multi_fill_result_type;

    void tick(void) override;
protected:
    way_iterator find_victim(size_t idx, cset_type&, const Transaction&) override;

    way_iterator repl_lru_mod(cset_type&, const Transaction&);
    way_iterator repl_rrip_mod(cset_type&, const Transaction&);
    
    inline void enqueue_writeback(Transaction trans) override
    {
        __TEMPLATE_PARENT__::enqueue_writeback(trans);

        size_t ch = dram_channel(trans.address),
               bank_idx = dram_bank_idx(trans.address);
        ++balance_counters_[ch][bank_idx];
        ++total_writebacks_[ch];
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "balanced_writeback_cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_BALANCED_WRITEBACK_CACHE_h
