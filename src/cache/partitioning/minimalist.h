/*
 *  author: Suhas Vittal
 *  date:   24 February 2025
 * */

#ifndef CACHE_PARTITIONING_MINIMALIST_h
#define CACHE_PARTITIONING_MINIMALIST_h

#include "cache/partitioning/ucp.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL>
class MinimalistPartitionManager : public PartitionManagerBase
{
private:
    using umon_type = UMON<IMPL>;
    /*
     * We use the utility monitors from UCP, but at a global scale. We are
     * testing how much associativity we need for the entire mix.
     * */
    umon_type umon_{};
    umon_type wmon_{};
    /*
     * This is whatever is left over from the workloads:
     * */
    size_t victim_part_ =0;

    std::ofstream mcp_logger_;
public:
    MinimalistPartitionManager(void);

    void update_partition(part_iterator begin, part_iterator end) override;
    /*
     * We want the sets tracked by `umon_` to exactly match what is in the cache.
     * Unlike UCP, we care about writeback fills.
     * */
    void update_on_probe(const Transaction& trans) override;
    void update_on_mark_dirty(const Transaction& trans) override;

    inline size_t get_victim_part(void) const
    {
        return victim_part_;
    }
private:
    void umon_fill(const Transaction&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "minimalist.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_PARTITIONING_MINIMALIST_h
