/*
 *  author: Suhas Vittal
 *  date:   11 January 2025
 * */

#include "dram/address.h"
#include "dram/enums.h"

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
        ++get_counter(address).writes;
    return hit;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::fill_result_type
__TEMPLATE_CLASS__::fill(uint64_t address, size_t num_refs, bool mark_dirty)
{
    auto out = __TEMPLATE_PARENT__::fill(address, num_refs, mark_dirty);

    if (!mark_dirty)
        --get_counter(address).reads;

    if (out.has_value() && out.value().dirty)
        ++get_counter(out.value().address).writes;

    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_mshr_init(uint64_t miss_address)
{
    ++get_counter(miss_address).reads;
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_dram_write_drain(size_t ch, size_t amt)
{
    // In an open page policy, it is possible for the counters to become out of sync
    // with DRAM due to row buffer hits. To handle this behavior, we simply reset the
    // counters.
    if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
    {
        ++write_epoch_[ch];
        if (write_epoch_[ch] == RESET_EPOCHS)
        {
            for (auto& c : counters_[ch])
                c.writes = 0;
            write_epoch_[ch] = 0;
            return;
        }
    }

    for (auto& c : counters_[ch])
    {
        c.writes -= amt;
        c.writes = std::clamp(c.writes, static_cast<ssize_t>(0), std::numeric_limits<ssize_t>::max());
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::find_victim(cset_type& s)
{
    size_t idx = __TEMPLATE_PARENT__::get_set_index(s[0].address);
    size_t ch = dram_channel(idx),
           bank_idx = dram_bank_idx(idx);

    const auto& ctrs = counters_[ch];
    auto r_it = std::min_element(ctrs.begin(), ctrs.end(),
                        [] (const auto& ctrx, const auto& ctry)
                        {
                            return ctrx.reads < ctry.reads;
                        });
    auto w_it = std::min_element(ctrs.begin(), ctrs.end(),
                        [] (const auto& ctrx, const auto& ctry)
                        {
                            return ctrx.writes < ctry.writes;
                        });
    bool is_critical = (counters_[ch][bank_idx].reads - r_it->reads < CRITICAL_READS)
                        && (counters_[ch][bank_idx].writes - w_it->writes >= CRITICAL_WRITES);

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

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::RWCounter&
__TEMPLATE_CLASS__::get_counter(uint64_t address)
{
    size_t ch = dram_channel(address);
    size_t idx = dram_bank_idx(address);
    return counters_[ch][idx];
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_PARENT__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
