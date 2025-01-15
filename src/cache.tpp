/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#include <algorithm>
#include <iostream>
#include <numeric>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <size_t SETS, size_t WAYS, CacheReplPolicy POL>
#define __TEMPLATE_CLASS__  Cache<SETS,WAYS,POL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::find_result_type
__TEMPLATE_CLASS__::find(uint64_t addr)
{
    cset_type& s = get_set(addr);
    auto it = std::find_if(s.begin(), s.end(),
                    [addr] (const CacheEntry& e)
                    {
                        return e.valid && e.address == addr;
                    });
    return std::make_tuple(&s, it);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe(uint64_t addr, bool write)
{
    if constexpr (POL == CacheReplPolicy::PERFECT)
        return true;

    auto [s_p, it] = find(addr);
    if (it == s_p->end())
        return false;
    else
    {
        update(*it);
        it->dirty |= write;
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::mark(uint64_t addr, bool as_dirty)
{
    if constexpr (POL == CacheReplPolicy::PERFECT)
        return true;

    auto [s_p, it] = find(addr);
    if (it == s_p->end())
        return false;
    else
    {
        it->dirty = as_dirty;
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::fill_result_type
__TEMPLATE_CLASS__::fill(uint64_t addr, size_t num_refs, bool mark_dirty)
{
    fill_result_type out;
    if constexpr (POL == CacheReplPolicy::PERFECT)
        return out;

    cset_type& s = get_set(addr);
    auto it = std::find_if_not(s.begin(), s.end(),
                        [] (const CacheEntry& e)
                        {
                            return e.valid;
                        });
    if (it == s.end())
    {
        it = find_victim(s); 
        out = *it;
    }
    *it = CacheEntry(addr, num_refs, mark_dirty);
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill_with_eager_writeback(uint64_t addr, size_t num_refs, bool mark_dirty)
{
    fill_result_type v, w;
    v = fill(addr, num_refs, mark_dirty);
    if (v.has_value())
    {
        // We obtain `w` by checking the new LRU position of the set.
        auto lru_it = get_way_in_lru_pos(get_set(addr));
        if (lru_it->dirty)
            w = *lru_it;
    }
    return multi_fill_result_type(v, w);
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::next_line_fill_result_type
__TEMPLATE_CLASS__::fill_with_next_line_writeback(uint64_t addr, size_t column_bit, size_t num_refs, bool mark_dirty)
{
    fill_result_type v, w;
    size_t w_lru_pos;

    v = fill(addr, num_refs, mark_dirty);

    if (v.has_value() && v.value().dirty)
    {
        const auto& e = v.value();
        uint64_t next_lineaddr = e.address ^ (1L << column_bit);
        if (get_set_index(e.address) != get_set_index(next_lineaddr))
        {
            std::cout << "set mismatch: " << get_set_index(e.address) << ", " << get_set_index(next_lineaddr) << "\n";
            exit(1);
        }
        cset_type& s = get_set(e.address);
        auto it = std::find_if(s.begin(), s.end(),
                            [next_lineaddr] (const CacheEntry& e)
                            {
                                return e.address == next_lineaddr;
                            });
        if (it != s.end() && it->dirty)
        {
            w = *it;
            w_lru_pos = std::count_if(s.begin(), s.end(),
                                [t=it->timestamp] (const auto& e)
                                {
                                    return t > e.timestamp;
                                });
        }
    }
    return next_line_fill_result_type(v, w, w_lru_pos);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::invalidate(uint64_t addr)
{
    auto [s_p, it] = find(addr);
    if (it != s_p->end())
        it->valid = false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
template <class PRED> inline size_t
__TEMPLATE_CLASS__::get_occupancy(const PRED& pred)
{
    size_t cnt = std::transform_reduce(csets_.begin(), csets_.end(), 
                        static_cast<size_t>(0),
                        std::plus<size_t>{},
                        [pred] (const cset_type& s)
                        {
                            return std::count_if(s.begin(), s.end(), pred);
                        });
    return cnt;
}

__TEMPLATE_HEADER__ size_t
__TEMPLATE_CLASS__::get_occupancy()
{
    return get_occupancy([] (const CacheEntry& e) { return e.valid; });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::cset_type::iterator
__TEMPLATE_CLASS__::find_victim(cset_type& s)
{
    if constexpr (POL == CacheReplPolicy::LRU)
        return get_way_in_lru_pos(s);
    else if constexpr (POL == CacheReplPolicy::RAND)
        return std::next( s.begin(), fast_mod<WAYS>(rng_()) );
    else if constexpr (POL == CacheReplPolicy::SRRIP)
    {
        auto v_it = std::min_element(s.begin(), s.end(),
                                [] (const CacheEntry& x, const CacheEntry& y)
                                {
                                    return x.rrpv < y.rrpv;
                                });
        if (v_it->rrpv > 0)
        {
            // Reduce all entries' rrpv values.
            for (CacheEntry& x : s)
                x.rrpv -= v_it->rrpv;
        }
        return v_it;
    } 
    else 
    {
        std::cerr << "unsupported cache replacement policy.\n";
        exit(1);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::update(CacheEntry& e)
{
    e.timestamp = GL_CYCLE;
    e.rrpv = SRRIP_MAX;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::cset_type::iterator
__TEMPLATE_CLASS__::get_way_in_lru_pos(cset_type& s)
{
    return std::min_element(s.begin(), s.end(),
                [] (const auto& x, const auto& y)
                {
                    return x.timestamp < y.timestamp;
                });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
