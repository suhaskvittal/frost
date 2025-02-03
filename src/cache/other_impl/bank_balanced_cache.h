/*
 *  author: Suhas Vittal
 *  date:   2 February 2025
 * */

#ifndef CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h
#define CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h

#include "constants.h"
#include "cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<IMPL,NUM_SETS,NUM_WAYS,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
class BankBalancedCache : public __TEMPLATE_PARENT__
{
protected:
    using write_counter_array = std::array<std::array<int8_t, DRAM_TOT_BANKS_PER_CHANNEL>, DRAM_CHANNELS>;

    constexpr static size_t CRITICAL_WRITES = (DRAM_WQ_SIZE / DRAM_TOT_BANKS_PER_CHANNEL) / 2;
    constexpr static int8_t CTR_MIN = 0;
    constexpr static int8_t CTR_MAX = 64;

    write_counter_array per_bank_write_counters_{};
public:
    using __TEMPLATE_PARENT__::Cache;
    using typename __TEMPLATE_PARENT__::cset_type;
    using typename __TEMPLATE_PARENT__::way_iterator;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    using typename __TEMPLATE_PARENT__::multi_fill_result_type;

    void handle_write_drain(size_t channel_id, size_t writes_drained_per_bank);
    void handle_write_drain(size_t channel_id, const write_counts_array&);
protected:
    multi_fill_result_type fill(uint64_t, size_t, bool) override;

    way_iterator find_victim(cset_type&);

    way_iterator lru_mod(cset_type&, bool evict_dirty);
    way_iterator rrip_mod(cset_type&, bool evict_dirty);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class>
struct is_bank_balanced_cache : std::false_type {};

template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
struct is_bank_balanced_cache<BankBalancedCache<IMPL, NUM_SETS, NUM_WAYS, NEXT_TYPE>> : std::true_type {};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "bank_balanced_cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h
