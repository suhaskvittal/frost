/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#include <algorithm>
#include <iostream>
#include <numeric>

#define __TEMPLATE_HEADER__ template <size_t SETS, size_t WAYS, CacheReplPolicy POL>
#define __TEMPLATE_CLASS__  Cache<SETS,WAYS,POL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe(uint64_t addr, bool write)
{
    if constexpr (POL == CacheReplPolicy::PERFECT)
        return true;

    cset_t& s = get_set(addr);
    auto it = std::find_if(s.begin(), s.end(),
                    [addr] (entry_t& e)
                    {
                        return e.valid && e.address == addr;
                    });
    if (it == s.end()) {
        return false;
    } else {
        update(*it);
        it->dirty = write;
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::mark_dirty(uint64_t addr)
{
    if constexpr (POL == CacheReplPolicy::PERFECT)
        return true;

    cset_t& s = get_set(addr);
    auto it = std::find_if(s.begin(), s.end(),
                    [addr] (entry_t& e)
                    {
                        return e.valid && e.address == addr;
                    });
    if (it == s.end()) {
        return false;
    } else {
        it->dirty = true;
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::fill_result_t
__TEMPLATE_CLASS__::fill(uint64_t addr, size_t num_refs)
{
    return fill(entry_t(addr, num_refs));
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::fill_result_t
__TEMPLATE_CLASS__::fill(entry_t&& e)
{
    fill_result_t out;
    if constexpr (POL == CacheReplPolicy::PERFECT)
        return out;

    cset_t& s = get_set(e.address);
    auto it = std::find_if_not(s.begin(), s.end(),
                        [] (entry_t& e)
                        {
                            return e.valid;
                        });
    if (it == s.end()) {
        it = find_victim(s); 
        out = *it;
    }
    *it = std::move(e);
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::invalidate(uint64_t addr)
{
    cset_t& s = get_set(addr);
    auto it = std::find_if(s.begin(), s.end(),
                    [addr] (entry_t& e)
                    {
                        return e.address == addr;
                    });
    it->valid = false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
template <class PRED> size_t
__TEMPLATE_CLASS__::get_occupancy(const PRED& pred)
{
    size_t cnt = std::accumulate(csets_.begin(), csets_.end(), static_cast<size_t>(0),
                        [pred] (size_t tot, const cset_t& s)
                        {
                            return tot + std::count_if(s.begin(), s.end(), pred);
                        });
    return cnt;
}

__TEMPLATE_HEADER__ size_t
__TEMPLATE_CLASS__::get_occupancy()
{
    return get_occupancy([] (const entry_t& e) { return e.valid; });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::cset_t::iterator
__TEMPLATE_CLASS__::find_victim(cset_t& s)
{
    if constexpr (POL == CacheReplPolicy::LRU) {
        return std::min_element(s.begin(), s.end(),
                                [] (const entry_t& x, const entry_t& y)
                                {
                                    return x.timestamp < y.timestamp;
                                });
    } else if constexpr (POL == CacheReplPolicy::RAND) {
        return std::next( s.begin(), fast_mod<WAYS>(rng_()) );
    } else if constexpr (POL == CacheReplPolicy::SRRIP) {
        auto v_it = std::min_element(s.begin(), s.end(),
                                [] (const entry_t& x, const entry_t& y)
                                {
                                    return x.rrpv < y.rrpv;
                                });
        if (v_it->rrpv > 0) {
            // Reduce all entries' rrpv values.
            for (entry_t& x : s)
                x.rrpv -= v_it->rrpv;
        }
        return v_it;
    } else {
        std::cerr << "unsupported cache replacement policy.\n";
        exit(1);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update(entry_t& e)
{
    e.timestamp = GL_CYCLE;
    e.rrpv = SRRIP_MAX;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
