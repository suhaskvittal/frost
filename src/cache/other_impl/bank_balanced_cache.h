/*
 *  author: Suhas Vittal
 *  date:   11 January 2025
 * */

#ifndef CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h
#define CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h

#include "constants.h"

#include "cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct ChannelWriteTracker
{
    using bank_counter_array_type = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;

    bank_counter_array_type ctrs{};
    size_t tot_writes_in_epoch =0;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * This class manages its writebacks so that writebacks do not overwhelming go into
 * a single bank. Instead, writebacks are bank-aware, and the cache will do its
 * best to try and balance out the writes.
 * */
#define __TEMPLATE_PARENT__ Cache<SETS,WAYS,POL>

template <size_t SETS, size_t WAYS, CacheReplPolicy POL>
class BankBalancedCache : public __TEMPLATE_PARENT__
{
private:
    constexpr static size_t CRITICAL_WRITES = DRAM_WQ_SIZE / DRAM_TOT_BANKS_PER_CHANNEL;

    using write_tracker_array_type = std::array<ChannelWriteTracker, DRAM_CHANNELS>;

    write_tracker_array_type trackers_{};
public:
    using __TEMPLATE_PARENT__::Cache; // inherit constructors and useful typedefs:
    using typename __TEMPLATE_PARENT__::cset_type;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    /*
     * On a `mark` clean, `trackers_` is updated.
     * */
    bool mark(uint64_t, bool as_dirty) override;
    /*
     * `fill` is updated to update (and potentially reset) `trackers_` on a writeback.
     * */
    fill_result_type fill(uint64_t, size_t num_refs) override;
protected:
    /*
     * This class modifies standard eviction policies to operate based
     * on the counters in `trackers_`
     * */
    typename cset_type::iterator find_victim(cset_type&) override;
private:
    size_t get_tracker_entry(uint64_t address);
    void increment_tracker(uint64_t address);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "bank_balanced_cache.tpp"

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h
