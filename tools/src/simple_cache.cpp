/*
 *  author: Suhas Vittal
 *  date:   3 February 2025
 * */

#include "simple_cache.h"

#include <algorithm>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SimpleCache::SimpleCache(size_t assoc, size_t sets)
    :assoc_(assoc),
    sets_(sets),
    csets_(sets, std::vector<Entry>(assoc))
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SimpleCache::ProbeResultType
SimpleCache::probe(uint64_t address, bool write)
{
    ++s_accesses_;

    cset_type& s = get_set(address);
    auto it = std::find_if(s.begin(), s.end(),
                    [address] (const auto& e) { return e.valid && e.address == address; });
    if (it == s.end())
    {
        // Check memento if enabled -- last chance:
        if (enable_memento_test_)
        {
            bool all_ways_valid = std::all_of(s.begin(), s.end(),
                                            [] (const auto& e) { return e.valid; });
            if (all_ways_valid)
            {
                // Compute LRU positions -- only want to search amongst LRU ways:
                std::unordered_map<uint64_t, size_t> lru_positions;
                std::transform(s.begin(), s.end(), std::inserter(lru_positions, lru_positions.begin()),
                        [&s] (const auto& e)
                        {
                            size_t p = std::count_if(s.begin(), s.end(),
                                            [t=e.timestamp] (const auto& x) { return x.timestamp < t; });
                            return std::make_pair(e.address, p);
                        });

                size_t max_lru_pos = std::max(static_cast<size_t>(4), assoc_ / 2);
                max_lru_pos = std::min(assoc_, max_lru_pos);

                auto lru_it = std::find_if(s.begin(), s.end(),
                                        [address, max_lru_pos, &lru_positions, this]
                                        (const auto& e)
                                        {
                                            size_t pos = lru_positions[e.address];
                                            if (pos >= max_lru_pos)
                                                return false;
                                            return this->memento_eviction_map_.count(e.address)
                                                    && this->memento_eviction_map_[e.address] == address;
                                        });

                if (lru_it != s.end())
                {
                    memento_eviction_map_[lru_it->address] = address;

                    // Evict LRU way and mark this as a hit:
                    lru_it->address = address;
                    lru_it->timestamp = init_count_;
                    lru_it->dirty = write;
                    ++init_count_;
                    
                    return ProbeResultType::HIT_BUT_DO_FILL;
                }
            }
        }

        ++s_misses_;
        return ProbeResultType::MISS;
    }
    else
    {
        it->timestamp = init_count_;
        it->dirty |= write;
        ++init_count_;
        return ProbeResultType::HIT;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SimpleCache::ProbeResultType
SimpleCache::mark_dirty(uint64_t address)
{
    cset_type& s = get_set(address);
    auto it = std::find_if(s.begin(), s.end(),
                    [address] (const auto& e) { return e.valid && e.address == address; });
    if (it == s.end())
    {
        return ProbeResultType::MISS;
    }
    else
    {
        it->dirty = true;
        return ProbeResultType::HIT;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SimpleCache::victim_type
SimpleCache::fill(uint64_t address, bool dirty)
{
    victim_type out;

    cset_type& s = get_set(address);
    
    auto it = std::find_if(s.begin(), s.end(),
                            [address] (const auto& e) { return e.valid && e.address == address; });
    if (it != s.end())
        return out;

    it = std::find_if_not(s.begin(), s.end(),
                            [] (const auto& e) { return e.valid; });
    if (it == s.end())
    {
        it = std::min_element(s.begin(), s.end(),
                        [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });

        // only record to miss trace if there is a victim
        // fmt: access count (init_count), incoming, outgoing
        if (record_miss_trace_)
        {
            // Want to get LRU ways (1/4 of set, but at least one way).
            uint32_t num_lru_ways = std::max(static_cast<size_t>(1), assoc_/2);
            auto lru_ways = get_lru_ways(s, num_lru_ways);

            // Write LRU ways, and then the incoming line
            gzwrite(miss_trace_, &num_lru_ways, 4);
            for (const auto& e : lru_ways)
                gzwrite(miss_trace_, &e.address, 8);
            gzwrite(miss_trace_, &address, 8);
        }

        if (enable_memento_test_)
        {
            memento_eviction_map_.insert({ it->address, address });
        }

        out.emplace(std::move(*it));
    }
    it->valid = true;
    it->dirty = dirty;
    it->address = address;
    it->timestamp = init_count_;
    ++init_count_;

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SimpleCache::cset_type
SimpleCache::get_lru_ways(const cset_type& s, size_t num_ways) const
{
    std::vector<Entry> lru_ways(num_ways);
    std::partial_sort_copy(s.begin(), s.end(), lru_ways.begin(), lru_ways.end(),
            [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
    return lru_ways;
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
