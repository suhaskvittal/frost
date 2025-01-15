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

    using write_tracker_type = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;
    using write_tracker_array_type = std::array<write_tracker_type, DRAM_CHANNELS>;

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
     * The new `fill` does two things:
     *  (1) If the number of writebacks to the bank is greater than or equal to `CRITICAL_WRITES` and
     *      the victim is dirty, then the fill is aborted. In this case, `fill_result_type` corresponds 
     *      to the cache-entry that would have been installed.
     *  (2) Increments the corresponding write counter.
     * */
    fill_result_type fill(uint64_t, size_t num_refs, bool mark_dirty=false) override;

    inline void decrement_write_counters(size_t channel_id, size_t amt)
    {
        for (size_t& ctr : trackers_[channel_id])
            ctr = (ctr >= amt) ? ctr - amt : 0;
    }
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
/*
 * Defining some template structures to check if a cache is a bank-balanced-cache.
 * */
template <class>
struct is_bank_balanced_cache : std::false_type {};

template <size_t SETS, size_t WAYS, CacheReplPolicy POL> 
struct is_bank_balanced_cache<BankBalancedCache<SETS,WAYS,POL>> : std::true_type {};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "bank_balanced_cache.tpp"

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h
