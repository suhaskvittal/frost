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
    auto out = __TEMPLATE_PARENT__::fill(address, num_refs, mark_dirty);
    if (out.has_value() && out.value().dirty)
        increment_tracker(out.value().address);
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::find_victim(cset_type& s)
{
    size_t idx = __TEMPLATE_PARENT__::get_set_index(s[0].address);
    size_t ch = dram_channel(idx),
           bank_idx = dram_bank_idx(idx);
    // Note that all entries in this set also belong to the same bank:
    size_t min_writes = *std::min_element(trackers_[ch].begin(), trackers_[ch].end());
    bool is_critical = (trackers_[ch][bank_idx]-min_writes) >= CRITICAL_WRITES / 2;

    if (is_critical)
        return find_victim_modified_policy(s);
    else
        return __TEMPLATE_PARENT__::find_victim(s);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::find_victim_modified_policy(cset_type& s)
{
    if constexpr (POL == CacheReplPolicy::LRU)
        return lru_mod(s);
    else if constexpr (POL == CacheReplPolicy::SRRIP)
        return rrip_mod(s);
    else
        return __TEMPLATE_PARENT__::find_victim(s);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::lru_mod(cset_type& s)
{
    return std::min_element(s.begin(), s.end(),
                [] (const auto& x, const auto& y)
                {
                    if (x.dirty == y.dirty)
                        return x.timestamp < y.timestamp;
                    else
                        return y.dirty;
                });
}

__TEMPLATE_HEADER__ inline typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::rrip_mod(cset_type& s)
{
    constexpr size_t RRPV_TOL = WAYS/4;

    auto v_it = std::min_element(s.begin(), s.end(),
                    [] (const auto& x, const auto& y)
                    {
                        if (x.dirty == y.dirty)
                            return x.rrpv < y.rrpv;
                        else
                        {
                            // Check if the `rrpv` is comparable. If so, choose dirty line.
                            // If not, default to `rrpv` comparison.
                            if (x.dirty && x.rrpv < y.rrpv)
                                return x.rrpv + RRPV_TOL < y.rrpv;
                            else if (y.dirty && y.rrpv < x.rrpv)
                                return y.rrpv + RRPV_TOL < x.rrpv;
                            else
                                return false;
                        }
                    });
    if (v_it->rrpv > 0)
    {
        for (auto& e : s)
            e.rrpv = (e.rrpv < v_it->rrpv) ? 0 : e.rrpv - v_it->rrpv;
    }
    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline size_t
__TEMPLATE_CLASS__::get_tracker_entry(uint64_t address) const
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
