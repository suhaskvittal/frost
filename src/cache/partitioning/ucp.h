/*
 *  author: Suhas Vittal
 *  date:   19 February 2025
 * */

#ifndef CACHE_PARTITIONING_UCP_h
#define CACHE_PARTITIONING_UCP_h

#include "cache/entry.h"
#include "util/numerics.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * UCP implementation:
 * */
template <class IMPL>
class UMON
{
public:
    using ctr_type  = size_t;
    using ctr_array = std::vector<ctr_type>;

    ctr_array hit_counters;
    ctr_type  total_misses =0;
private:
    struct internal_cache_type
    {
        constexpr static size_t NUM_SETS = IMPL::NUM_SETS;

        cset_array csets;

        internal_cache_type(void)
            :csets(NUM_SETS, cset_type(IMPL::NUM_WAYS, CacheEntry{}))
        {}
    };

    constexpr static size_t SET_MODULUS = IMPL::NUM_SETS / internal_cache_type::NUM_SETS;

    internal_cache_type atd;
public:
    UMON(void);

    inline ctr_type utility(size_t w) const
    {
        return std::reduce(hit_counters.begin() + w, hit_counters.end(), total_misses);
    }

    inline ctr_type utility_difference(size_t a, size_t b) const
    {
        return std::reduce(hit_counters.begin()+a, hit_counters.begin()+b, 0);
    }

    void atd_probe(const Transaction&);
    void atd_fill(const Transaction&);

    cset_array::iterator atd_set_lookup(const Transaction&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL>
class UCPManager : public PartitionManagerBase
{
private:
    using umon_array = std::array<UMON<IMPL>, NUM_THREADS>;

    umon_array umon_{};

    std::ofstream ucp_logger_{};
public:
    UCPManager(void);
    /*
     * This function updates the partition sizes in the container from `begin` to `end`.
     * */
    void update_partition(part_iterator begin, part_iterator end) override;
    /*
     * Both of the following functions are straightforward and can be inline:
     * */
    inline void update_on_access(const Transaction& trans) override
    {
        if (trans.coreid >= NUM_THREADS)
            return;
        umon_[trans.coreid].atd_probe(trans);
    }

    inline void update_on_fill(const Transaction& trans) override
    {
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "ucp.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_PARTITIONING_UCP_h
