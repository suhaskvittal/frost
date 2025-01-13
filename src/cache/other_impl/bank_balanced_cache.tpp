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
__TEMPLATE_CLASS__::fill(uint64_t address, size_t num_refs)
{
    auto out = __TEMPLATE_PARENT__::fill(address, num_refs);
    if (out.has_value() && out.value().dirty)
        increment_tracker(out.value().address);
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::find_victim(cset_type& s)
{
    // Note that all entries in this set also belong to the same bank:
    bool is_critical = get_tracker_entry(s[0].address) >= CRITICAL_WRITES;

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
    size_t idx = get_bank_idx(address);
    return trackers_[ch].ctrs[idx];
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::increment_tracker(uint64_t address)
{
    size_t ch = dram_channel(address);
    size_t idx = get_bank_idx(address);
    ++trackers_[ch].ctrs[idx];
    ++trackers_[ch].tot_writes_in_epoch;

    if (trackers_[ch].tot_writes_in_epoch == DRAM_WQ_SIZE)
    {
        // Reset the tracker.
        trackers_[ch].ctrs.fill(0);
        trackers_[ch].tot_writes_in_epoch = 0;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_PARENT__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
