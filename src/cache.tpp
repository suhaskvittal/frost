/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#include <algorithm>
#include <iostream>

#define __TEMPLATE_HEADER__ template <size_t SETS, size_t WAYS, CacheReplPolicy POL>
#define __TEMPLATE_CLASS__  Cache<SETS,WAYS,POL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe(uint64_t addr)
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
__TEMPLATE_CLASS__::fill(uint64_t addr)
{
    return fill(entry_t(addr));
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::fill_result_t
__TEMPLATE_CLASS__::fill(entry_t&& e)
{
    fill_result_t out;

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

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::cset_t::iterator
__TEMPLATE_CLASS__::find_victim(cset_t& s)
{
    if constexpr (POL == CacheReplPolicy::LRU) {
        return std::min_element(s.begin(), s.end(),
                                [] (entry_t& x, entry_t& y)
                                {
                                    return x.timestamp < y.timestamp;
                                });
    } else if constexpr (POL == CacheReplPolicy::RAND) {
        return std::next( s.begin(), numeric_traits<WAYS>::mod(rng_()) );
    } else {
        std::cerr << "unsupported cache replacement policy.\n";
        exit(1);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
