/*
 *  author: Suhas Vittal
 *  date:   11 January 2025
 * */

#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <size_t SETS, size_t WAYS, CacheReplPolicy POL>
#define __TEMPLATE_CLASS__ BankBalancedCache<SETS,WAYS,POL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::mark(uint64_t address, bool as_dirty)
{
    bool hit = __TEMPLATE_PARENT__::mark(address, as_dirty);
    if (hit && !as_dirty)
        increment_tracker(address);
    return hit;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::fill_result_type
__TEMPLATE_CLASS__::fill(uint64_t address, size_t num_refs, bool mark_dirty)
{
    fill_result_type out;
    if constexpr (POL == CacheReplPolicy::PERFECT)
        return out;

    cset_type& s = __TEMPLATE_PARENT__::get_set(address);
    auto it = std::find_if_not(s.begin(), s.end(),
                        [] (const CacheEntry& e)
                        {
                            return e.valid;
                        });
    if (it == s.end())
    {
        it = find_victim(s); 
        if (it->dirty)
        {
            increment_tracker(it->address);
        }
        out = *it;
    }
    *it = CacheEntry(address, num_refs, mark_dirty);
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::find_victim(cset_type& s)
{
    size_t ch = dram_channel(s[0].address),
           bank_idx = dram_bank_idx(s[0].address);
    size_t min_writes = *std::min_element(trackers_[ch].begin(), trackers_[ch].end());
    // Note that all entries in this set also belong to the same bank:
    bool is_critical = (trackers_[ch][bank_idx] - min_writes) >= CRITICAL_WRITES/2;

    if constexpr (POL == CacheReplPolicy::LRU)
    {
        return std::min_element(s.begin(), s.end(),
                        [is_critical] (const auto& x, const auto& y)
                        {
                            if (is_critical)
                            {
                                if (x.dirty == y.dirty)
                                    return x.timestamp < y.timestamp;
                                else
                                    return y.dirty;  // Want to evict the clean element, so if y is dirty, evict x.
                            }
                            else
                                return x.timestamp < y.timestamp;
                        });
    }
    else if constexpr (POL == CacheReplPolicy::RAND)
        return __TEMPLATE_PARENT__::find_victim(s);
    else if constexpr (POL == CacheReplPolicy::SRRIP)
    {
        auto v_it = std::min_element(s.begin(), s.end(),
                                [is_critical] (const auto& x, const auto& y)
                                {
                                    if (is_critical)
                                    {
                                        if (x.dirty == y.dirty)
                                            return x.rrpv < y.rrpv;
                                        else
                                            return y.dirty;
                                    }
                                    else
                                        return x.rrpv < y.rrpv;
                                });
        if (v_it->rrpv > 0)
        {
            for (auto& x : s)
            {
                if (x.rrpv > v_it->rrpv)
                    x.rrpv -= v_it->rrpv;
                else
                    x.rrpv = 0;
            }
        }
        return v_it;
    }
    else
        return __TEMPLATE_PARENT__::find_victim(s);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline size_t
__TEMPLATE_CLASS__::get_tracker_entry(uint64_t address)
{
    size_t ch = dram_channel(address);
    size_t idx = dram_bank_idx(address);
    return trackers_[ch][idx];
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::increment_tracker(uint64_t address)
{
    size_t ch = dram_channel(address);
    size_t idx = dram_bank_idx(address);
    ++trackers_[ch][idx];
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_PARENT__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
