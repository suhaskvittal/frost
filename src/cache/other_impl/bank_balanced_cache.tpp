/*
 *  author: Suhas Vittal
 *  date:   3 February 2025
 * */

#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ BankBalancedCache<IMPL, NUM_SETS, NUM_WAYS, NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_write_drain(size_t channel_id, size_t w)
{
    for (auto& c : per_bank_write_counters_[channel_id])
    {
        c -= w;
        c = std::clamp(c, CTR_MIN, CTR_MAX);
    }
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_write_drain(size_t channel_id, const write_counts_array& w)
{
    for (size_t i = 0; i < w.size(); i++)
    {
        auto& c = per_bank_write_counters_[channel_id][i];
        c -= w.at(i);
        c = std::clamp(c, CTR_MIN, CTR_MAX);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill(uint64_t address, size_t num_refs, bool dirty)
{
    multi_fill_result_type out = __TEMPLATE_PARENT__::fill(address, num_refs, dirty);
    
    for (const auto& v : out)
    {
        if (v.entry.dirty)
        {
            size_t bank_idx = dram_bank_idx(v.entry.address);
            size_t channel = dram_channel(v.entry.address);
            ++per_bank_write_counters_[channel][bank_idx];
            break;
        }
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(cset_type& s)
{
    size_t bank_idx = dram_bank_idx(s[0].address);
    size_t channel = dram_channel(s[0].address); 
    auto& ctrs = per_bank_write_counters_[channel];

    // Check that this bank has not gone critical.
    uint64_t w = ctrs[bank_idx];
    auto [min_it, max_it] = std::minmax_element(ctrs.begin(), ctrs.end());

    bool evict_clean = (w - *min_it) >= CRITICAL_WRITES;
    bool evict_dirty = (*max_it - w) >= CRITICAL_WRITES;
    
    // If we have gone critical, then use a modified replacement policy:
    if (evict_clean || evict_dirty)
    {
        if constexpr (IMPL::REPL == CacheReplPolicy::LRU)
            return lru_mod(s, evict_dirty);
        else if constexpr (IMPL::REPL == CacheReplPolicy::RAND)
            return rand(s);
        else if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP)
            return rrip_mod(s, evict_dirty);
        else if constexpr (IMPL::REPL == CacheReplPolicy::DRRIP)
        {
            update_psel(cache_set_index<NUM_SETS>(s[0].address));
            return rrip_mod(s);
        }
        else
        {
            std::cerr << "unsupported (modified) cache replacement policy.\n";
            exit(1);
        }
    }
    else
        return __TEMPLATE_PARENT__::find_victim(s);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::lru_mod(cset_type& s, bool evict_dirty)
{
    std::unordered_map<uint64_t, size_t> lru_pos_map;
    lru_pos_map.reserve(NUM_WAYS);

    std::transform(s.begin(), s.end(), std::inserter(lru_pos_map, lru_pos_map.begin()),
                [&s] (const auto& e) 
                { 
                    size_t p = cset_get_lru_position_of_entry(e, s.cbegin(), s.cend());
                    return std::make_pair(e.address, p);
                });

    return std::min_element(s.begin(), s.end(),
                        [evict_dirty, &lru_pos_map] (const auto& x, const auto& y)
                        {
                            size_t px = lru_pos_map.at(x.address),
                                   py = lru_pos_map.at(y.address);
                            if (px < 4 && py < 4)
                            {
                                if (x.dirty == y.dirty)
                                    return x.timestamp < y.timestamp;
                                else
                                    return evict_dirty ? x.dirty : y.dirty;
                            }
                            else
                                return x.timestamp < y.timestamp;
                        });
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::rrip_mod(cset_type& s, bool evict_dirty)
{
    return std::min_element(s.begin(), s.end(),
                        [evict_dirty] (const auto& x, const auto& y)
                        {
                            if (x.dirty == y.dirty)
                                return x.rrpv < y.rrpv;
                            else
                                return evict_dirty ? x.dirty : y.dirty;
                        });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
