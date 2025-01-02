/*
 *  author: Suhas Vittal
 *  date:   1 January 2024
 * */

#ifndef CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h
#define CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h

#include "globals.h"

#include "cache.h"
#include "dram/address.h"

#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class CACHE_TYPE>
class VirtualWriteQueue
{
public:
    const size_t high_watermark_;
    const size_t low_watermark_;
private:
    using cache_ptr = std::unique_ptr<CACHE_TYPE>;
    using criticality_tracker_t = std::unordered_map<size_t, size_t>;
    /*
     * Dram resource tuple: (channel, rank, bankgroup, bank)
     * */
    using dram_resource_tuple_t = std::tuple<size_t, size_t, size_t, size_t>;
    using common_resource_map_t = std::unordered_map<dram_resource_tuple_t, std::vector<size_t>>;
    using dirty_count_array_t = std::array<size_t, DRAM_CHANNELS>;

    cache_ptr& cache_;
    /*
     * Keeps track of all indices of sets that are critical (dirty ways near the LRU position). The
     * values are the number of dirty ways near the LRU position.
     * */
    criticality_tracker_t critical_counts_;
    /*
     * Maintains a map of DRAM locations to cache sets.
     * */
    common_resource_map_t dram_common_map_; 

    dirty_count_array_t dirty_count_{};
public:
    using writeback_t = std::optional<CacheEntry>;
    using write_hit_probe_t = std::vector<CacheEntry>;

    VirtualWriteQueue(cache_ptr&);

    bool probe_with_criticality_update(uint64_t address);
    bool mark_with_criticality_update(uint64_t address, bool as_dirty);
    void update_criticality(size_t set_idx);
    writeback_t schedule_writeback(size_t channel_idx);
    write_hit_probe_t harvest_write_row_hits(uint64_t base_address);

    inline size_t get_count(size_t i) const { return dirty_count_.at(i); }
private:
    using lru_ways_t = std::vector<CacheEntry>;
    /*
     * This function enables a very fast update to the criticality of the given set (with index `idx`)
     * if we have already computed its `lru_ways` and are cleaning a line in the set.
     * */
    void update_criticality_after_scheduled_writeback(size_t idx, uint64_t cleaned_address, const lru_ways_t&);

    lru_ways_t get_lru_ways(const CACHE_TYPE::cset_t&);
    size_t count_dirty_lru_ways(const CACHE_TYPE::cset_t&);
    /*
     * Need to use these functions to manipluate `critical_counts_` so `dirty_cnt_` is also updated.
     * */
    void set_critical_count(size_t idx, size_t to);
    void increment_critical_count(size_t);
    void decrement_critical_count(size_t);

    inline constexpr size_t lru_ways(void) const { return cache_->num_ways() / 4: }
    inline dram_resource_tuple_t make_resource_key(uint64_t address)
    {
        return dram_resource_tuple_t{dram_channel(address), dram_rank(address),
                                dram_bankgroup(address), dram_bank(address)};
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "virtual_write_queue.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h
