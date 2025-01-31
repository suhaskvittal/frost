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
__TEMPLATE_CLASS__::probe(uint64_t address, bool write)
{
    bool hit = __TEMPLATE_PARENT__::probe(address, write);
    if (hit)
        ++hit_counter_;
    ++access_counter_;
    return hit;
}

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
__TEMPLATE_CLASS__::handle_mshr_init(uint64_t address)
{
    ++get_counter(address).reads;
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_write_bypass(uint64_t address)
{
    ++get_counter(address).writes;
}

__TEMPLATE_HEADER__ void
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

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::BalanceLevel
__TEMPLATE_CLASS__::get_write_balance_level(uint64_t address)
{
    size_t ch = dram_channel(address),
           bank_idx = dram_bank_idx(address);

    const auto& ctrs = counters_[ch];
    BalanceLevel b = compute_balance_level(ctrs[bank_idx], ctrs);
    return b;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::fill_will_replace_noncritical_victim(uint64_t address, BalanceLevel b)
{
    if (b == BalanceLevel::OK)
        return __TEMPLATE_PARENT__::fill_will_replace_noncritical_victim(address);
    else
    {
        const cset_type& s = __TEMPLATE_PARENT__::get_const_set(address);
        return std::any_of(s.begin(), s.end(),
                            [want_dirty = (b==BalanceLevel::REPL_DIRTY)] 
                            (const auto& e) { return !e.valid || (e.likely_dead && e.dirty == want_dirty); });
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
    BalanceLevel b = compute_balance_level(ctrs[bank_idx], ctrs);

    if (b != BalanceLevel::OK)
    {
        auto v_it = std::find_if(s.begin(), s.end(),
                        [want_dirty = (b == BalanceLevel::REPL_DIRTY)] 
                        (const auto& e) { return e.likely_dead && (e.dirty == want_dirty); });
        if (v_it != s.end())
            return v_it;

        // Otherwise, use the normal replacement policy:
        if constexpr (POL == CacheReplPolicy::LRU)
            return lru_mod(s, b);
        else if constexpr (POL == CacheReplPolicy::SRRIP)
            return rrip_mod(s, b);
        else
        {
            std::cerr << "bank balanced cache currently does not support the given replacement policy.\n";
            exit(1);
        }
    }
    else
        return __TEMPLATE_PARENT__::find_victim(s);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::find_victim_modified_policy(cset_type& s, BalanceLevel b)
{
    if constexpr (POL == CacheReplPolicy::LRU)
        return lru_mod(s, b);
    else if constexpr (POL == CacheReplPolicy::SRRIP)
        return rrip_mod(s, b);
    else
        return __TEMPLATE_PARENT__::find_victim(s);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::lru_mod(cset_type& s, BalanceLevel b)
{
    constexpr size_t LRU_TOL = WAYS/8;

    std::unordered_map<uint64_t, size_t> lru_pos;
    lru_pos.reserve(WAYS);
    std::transform(s.begin(), s.end(), std::inserter(lru_pos, lru_pos.end()),
                    [&s] (const auto& e)
                    {
                        size_t p = std::count_if(s.begin(), s.end(),
                                    [t=e.timestamp] (const auto& x) { return x.timestamp < t; });
                        return std::make_pair(e.address, p);
                    });
    return std::min_element(s.begin(), s.end(),
                [b, &lru_pos] (const auto& x, const auto& y)
                {
                    if (x.dirty == y.dirty)
                        return x.timestamp < y.timestamp;
                    else
                    {
                        size_t px = lru_pos[x.address],
                               py = lru_pos[y.address];
                        if ((b == BalanceLevel::REPL_DIRTY && x.dirty) || (b == BalanceLevel::REPL_CLEAN && !x.dirty))
                            return px <= py + LRU_TOL;
                        else
                            return py > px + LRU_TOL;
                    }
                });
}

__TEMPLATE_HEADER__ typename __TEMPLATE_PARENT__::cset_type::iterator
__TEMPLATE_CLASS__::rrip_mod(cset_type& s, BalanceLevel b)
{
    auto v_it = std::min_element(s.begin(), s.end(),
                    [b] (const auto& x, const auto& y)
                    {
                        if (x.dirty == y.dirty)
                            return x.rrpv < y.rrpv;
                        else if (b == BalanceLevel::REPL_CLEAN)
                            return y.dirty;
                        else
                            return x.dirty;
                    });
    for (auto& e : s)
        e.rrpv = (e.rrpv < v_it->rrpv) ? 0 : e.rrpv - v_it->rrpv;
    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::BalanceLevel
__TEMPLATE_CLASS__::compute_balance_level(const RWCounter& c, const rw_counter_subarray_type& ctrs)
{
    auto [w_min_it, w_max_it] = std::minmax_element(ctrs.begin(), ctrs.end(),
                                    [] (const auto& x, const auto& y)
                                    {
                                        return x.writes < y.writes;
                                    });
    BalanceLevel bw = compute_balance_level_given_minmax(c.writes, w_min_it->writes, w_max_it->writes, true);

    return bw;
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::BalanceLevel
__TEMPLATE_CLASS__::compute_balance_level_given_minmax(ssize_t x, ssize_t min, ssize_t max, bool write)
{
    const ssize_t crit = write ? CRITICAL_WRITES : CRITICAL_READS;

    bool lower_cond = (x - min) >= crit,    // too many
         upper_cond = (max - x) >= crit;    // too few

    if (lower_cond && upper_cond)
        return BalanceLevel::OK;
    else if (lower_cond)
        return write ? BalanceLevel::REPL_CLEAN : BalanceLevel::REPL_DIRTY;
    else
        return write ? BalanceLevel::REPL_DIRTY : BalanceLevel::REPL_CLEAN;
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
