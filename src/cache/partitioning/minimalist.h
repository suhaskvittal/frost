/*
 *  author: Suhas Vittal
 *  date:   24 February 2025
 * */

#ifndef CACHE_PARTITIONING_MINIMALIST_h
#define CACHE_PARTITIONING_MINIMALIST_h

#include "cache/partitioning/ucp.h"
#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL>
class MinimalistPartitionManager : public PartitionManagerBase
{
private:
    constexpr static size_t UMON_SETS = 128;
    constexpr static size_t UMON_SET_MODULUS = IMPL::NUM_SETS / UMON_SETS;

    using channel_bitvec_type = std::array<bool, DRAM_CHANNELS>;
    using umon_type = UMON<IMPL, UMON_SETS>;

    using set_counter_array = std::array<size_t, UMON_SETS>;
    using way_bucket_array = std::vector<size_t>;

    channel_bitvec_type write_mode_{};
    /*
     * We use the utility monitor from UCP, but at a global scale. We are
     * testing how much associativity we need for the entire mix.
     * */
    umon_type         umon_{};
    /*
     * `wb_counters_` tracks the number of writebacks from a sampled set. Drained when a channel enters write mode.
     *
     * `wb_buckets_` is updated when a channel enters write mode and counts the number of sets that have writebacks
     * falling into a given bucket. This will have `IMPL::NUM_WAYS+1` entries (we need one entry for 0 writebacks).
     *
     * If we have more than `IMPL::NUM_WAYS` writebacks from a set, then we simply increment the last way bucket.
     * */
    set_counter_array wb_counters_{};
    way_bucket_array wb_buckets_{};
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

    inline void toggle_write_mode(size_t channel_id, bool w)
    {
        write_mode_[channel_id] = w;
    }

    inline size_t get_victim_part(void) const
    {
        return victim_part_;
    }
private:
    void umon_fill(const Transaction&);

    inline ssize_t wb_utility(size_t w) const
    {
        return std::reduce(wb_buckets_.begin()+w+1, wb_buckets_.end(), 0);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "minimalist.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_PARTITIONING_MINIMALIST_h
