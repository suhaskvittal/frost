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
    auto v_it = s.end();

    size_t ch = dram_channel(idx),
           bank_idx = dram_bank_idx(idx);
    // Note that all entries in this set also belong to the same bank:
    size_t min_writes = *std::min_element(trackers_[ch].begin(), trackers_[ch].end());
    bool is_critical = (trackers_[ch][bank_idx]-min_writes) >= CRITICAL_WRITES / 2;
    if (is_critical)
    {
        SetDuelingRole r = get_set_role(idx);

        if (r == SetDuelingRole::FOLLOWER)
        {
            uint64_t lhs = duel_latencies_[0]*duel_accesses_[1],
                     rhs = duel_latencies_[1]*duel_accesses_[0];
            r = (lhs < rhs) ? SetDuelingRole::LEADER_1 : SetDuelingRole::LEADER_2;
        }
        
        if (r == SetDuelingRole::LEADER_1)
        {
            ++s_repl_pol1_;
            v_it = __TEMPLATE_PARENT__::find_victim(s);
        }
        else
        {
            ++s_repl_pol2_;
            v_it = find_victim_second_policy(s);
        }
    }
    else
        v_it = __TEMPLATE_PARENT__::find_victim(s);
    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::find_victim_second_policy(cset_type& s)
{
    if constexpr (POL == CacheReplPolicy::LRU)
    {
        return std::min_element(s.begin(), s.end(),
                        [] (const auto& x, const auto& y)
                        {
                            if (x.dirty == y.dirty)
                                return x.timestamp < y.timestamp;
                            else
                                return y.dirty;  // Want to evict the clean element, so if y is dirty, evict x.
                        });
    }
    else if constexpr (POL == CacheReplPolicy::RAND)
        return __TEMPLATE_PARENT__::find_victim(s);
    else if constexpr (POL == CacheReplPolicy::SRRIP)
    {
        auto v_it = std::min_element(s.begin(), s.end(),
                                [] (const auto& x, const auto& y)
                                {
                                    if (x.dirty == y.dirty)
                                        return x.rrpv < y.rrpv;
                                    else
                                        return y.dirty;
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

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::SetDuelingRole
__TEMPLATE_CLASS__::get_set_role(size_t idx) const
{
    size_t grp = idx >> numeric_traits<LEADER_SETS>::log2,
           offset = fast_mod<LEADER_SETS>(idx);
    size_t compl_offset = offset ^ (LEADER_SETS-1);

    if (grp == offset)
        return SetDuelingRole::LEADER_1;
    else if (grp == compl_offset)
        return SetDuelingRole::LEADER_2;
    else
        return SetDuelingRole::FOLLOWER;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_PARENT__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
