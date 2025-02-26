/*
 *  author: Suhas Vittal
 *  date:   19 February 2025
 * */

#ifndef CACHE_PARTITIONING_UCP_h
#define CACHE_PARTITIONING_UCP_h

#include "cache/entry.h"
#include "cache/ext/atd.h"
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
struct UMON
{
    using ctr_type  = size_t;
    using ctr_array = std::vector<ctr_type>;
    using atd_type = AuxTagDirectory<IMPL, 64>;

    ctr_array hit_counters;
    ctr_type  total_misses =0;

    atd_type atd{};

    UMON(void)
        :hit_counters(IMPL::NUM_WAYS, 0)
    {}

    inline ctr_type utility(size_t w) const
    {
        return std::reduce(hit_counters.begin()+w, hit_counters.end(), total_misses);
    }

    inline ctr_type utility_difference(size_t a, size_t b) const
    {
        return std::reduce(hit_counters.begin()+a, hit_counters.begin()+b, 0);
    }
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
    void update_on_probe(const Transaction& trans) override;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "ucp.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_PARTITIONING_UCP_h
