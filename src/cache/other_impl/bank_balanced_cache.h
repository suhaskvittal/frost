/*
 *  author: Suhas Vittal
 *  date:   11 January 2025
 * */

#ifndef CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h
#define CACHE_OTHER_IMPL_BANK_BALANCED_CACHE_h

#include "constants.h"

#include "cache.h"
#include "util/numerics.h"

#include <algorithm>

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
public:
    enum class BalanceLevel { OK, REPL_CLEAN, REPL_DIRTY };

    constexpr static uint64_t COUNTER_REDUCE_CYCLES = 1024*1024;
    constexpr static size_t   COUNTER_REDUCE_SHIFT = 4;  // Reduces counters by 1/16 (right shift by 4).

    uint64_t s_repl_pol1_ =0;
    uint64_t s_repl_pol2_ =0;
private:
    constexpr static size_t CRITICAL_WRITES = (DRAM_WQ_SIZE / DRAM_TOT_BANKS_PER_CHANNEL) / 2;
    constexpr static size_t CRITICAL_READS = 2;
    constexpr static size_t RESET_EPOCHS = 128;

    struct RWCounter
    {
        ssize_t reads =0;
        ssize_t writes =0;
    };

    using rw_counter_subarray_type = std::array<RWCounter, DRAM_TOT_BANKS_PER_CHANNEL>;
    using rw_counter_array_type = std::array<rw_counter_subarray_type, DRAM_CHANNELS>;
    using write_epoch_array_type = std::array<size_t, DRAM_CHANNELS>;

    rw_counter_array_type counters_{};
    write_epoch_array_type write_epoch_{};
    /*
     * `hit_counter_` and `access_counter_` are used to override the dead block prediction policy.
     * If the miss rate is high (hit rate is low), the policy is overridden, and the LLC is used 
     * as a staging area for writebacks.
     * */
    uint64_t hit_counter_ =0;
    uint64_t access_counter_ =0;
public:
    using __TEMPLATE_PARENT__::Cache; // inherit constructors and useful typedefs:
    using typename __TEMPLATE_PARENT__::cset_type;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    /*
     * On a `mark` clean, `trackers_` is updated.
     * */
    bool probe(uint64_t, bool write=false) override;
    bool mark(uint64_t, bool as_dirty) override;
    /*
     * The new `fill` does two things:
     *  (1) If the number of writebacks to the bank is greater than or equal to `CRITICAL_WRITES` and
     *      the victim is dirty, then the fill is aborted. In this case, `fill_result_type` corresponds 
     *      to the cache-entry that would have been installed.
     *  (2) Increments the corresponding write counter.
     * */
    fill_result_type fill(uint64_t, size_t num_refs, bool mark_dirty=false) override;

    void handle_mshr_init(uint64_t address);
    void handle_write_bypass(uint64_t address);
    void handle_dram_write_drain(size_t channel_id, size_t amt);

    BalanceLevel get_write_balance_level(uint64_t address);

    bool fill_will_replace_noncritical_victim(uint64_t address, BalanceLevel);

    inline bool should_override_dead_block_predictor(void) const
    {
        return access_counter_ >= 1024  // Ensure that we have enough "samples"
                && hit_counter_ < (access_counter_ >> 6);  // This means hit rate < 1/64
    }

    inline void reduce_hit_rate_counters(void)
    {
        hit_counter_ >>= COUNTER_REDUCE_SHIFT;
        access_counter_ >>= COUNTER_REDUCE_SHIFT;
    }
protected:
    /*
     * This class modifies standard eviction policies to operate based
     * on the counters in `counters_`
     * */
    typename cset_type::iterator find_victim(cset_type&) override;
    typename cset_type::iterator find_victim_modified_policy(cset_type&, BalanceLevel);

    typename cset_type::iterator lru_mod(cset_type&, BalanceLevel);
    typename cset_type::iterator rrip_mod(cset_type&, BalanceLevel);

    BalanceLevel compute_balance_level(const RWCounter&, const rw_counter_subarray_type&);
    BalanceLevel compute_balance_level_given_minmax(ssize_t, ssize_t min, ssize_t max, bool is_write);
private:
    RWCounter& get_counter(uint64_t address);
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
