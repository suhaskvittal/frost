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
        update_entry(*it);
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
                        [] (const auto& e) { return e.valid; });
    if (it == s.end())
    {
        it = find_victim(s); 
        out = *it;
    }
    init_entry(*it, addr, num_refs, mark_dirty);
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

__TEMPLATE_HEADER__  inline bool
__TEMPLATE_CLASS__::fill_will_replace_invalid_victim(uint64_t address) const
{
    const cset_type& s = get_const_set(address);
    return std::any_of(s.begin(), s.end(), 
                        [] (const auto& e) { return !e.valid; });
}

__TEMPLATE_HEADER__  inline bool
__TEMPLATE_CLASS__::fill_will_replace_noncritical_victim(uint64_t address) const
{
    const cset_type& s = get_const_set(address);
    return std::any_of(s.begin(), s.end(), 
                        [] (const auto& e) { return !e.valid || e.likely_dead; });
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

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::mark_likely_dead(uint64_t address)
{
    auto [s_p, it] = find(address);
    if (it != s_p->end())
    {
        it->likely_dead = true;
        // Update `rrpv` as well:
        it->rrpv = 0;
    }
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::mark_likely_alive(uint64_t address)
{
    auto [s_p, it] = find(address);
    if (it != s_p->end())
    {
        it->likely_dead = false;
        // Update `rrpv` as well:
        it->rrpv = RRIP_MAX;
    }
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
    auto v_it = get_likely_dead_line(s);
    if (v_it != s.end())
        return v_it;

    if constexpr (POL == CacheReplPolicy::LRU)
        return lru(s);
    else if constexpr (POL == CacheReplPolicy::RAND)
        return rand(s);
    else if constexpr (POL == CacheReplPolicy::SRRIP)
        return rrip(s);
    else if constexpr (POL == CacheReplPolicy::DRRIP)
    {
        update_psel(get_set_index(s[0].address));
        return rrip(s);
    }
    else 
    {
        std::cerr << "unsupported cache replacement policy.\n";
        exit(1);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::cset_type::iterator
__TEMPLATE_CLASS__::lru(cset_type& s)
{
    return get_way_in_lru_pos(s);
}

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::cset_type::iterator
__TEMPLATE_CLASS__::rand(cset_type& s)
{
    return std::next(s.begin(), fast_mod<WAYS>(rng_()));
}

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::cset_type::iterator
__TEMPLATE_CLASS__::rrip(cset_type& s)
{
    auto v_it = std::min_element(s.begin(), s.end(),
                            [] (const CacheEntry& x, const CacheEntry& y)
                            {
                                return x.rrpv < y.rrpv;
                            });
    // Reduce all entries' rrpv values.
    for (CacheEntry& x : s)
        x.rrpv -= v_it->rrpv;
    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_entry(CacheEntry& e)
{
    e.timestamp = GL_CYCLE;
    e.rrpv = RRIP_MAX;
    e.likely_dead = false;
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::init_entry(CacheEntry& e, uint64_t addr, size_t num_refs, bool mark_dirty)
{
    e.valid = true;
    e.dirty = mark_dirty;
    e.address = addr;
    e.timestamp = GL_CYCLE;
    e.likely_dead = false;

    if constexpr (POL == CacheReplPolicy::DRRIP)
    {
        // Check whether or not to use BRRIP.
        size_t s = get_set_index(addr);
        SetDuelingRole r = get_set_role(s);
        // Resolve `r` if it is a follower set:
        if (r == SetDuelingRole::FOLLOWER)
            r = psel_ < PSEL_THRESHOLD ? SetDuelingRole::LEADER_1 : SetDuelingRole::LEADER_2;

        uint8_t rrip_init = (r == SetDuelingRole::LEADER_1) ? 1 : (bimodal_ctr_ == BIMODAL_CTR_MAX ? 1 : 0);
        e.rrpv = (num_refs > 1) ? RRIP_MAX : rrip_init;

        if (r == SetDuelingRole::LEADER_2)
        {
            fast_increment_and_mod_inplace<BIMODAL_CTR_MAX>(bimodal_ctr_);
            ++s_dueling_pol2_installs_;
        }
        else
            ++s_dueling_pol1_installs_;
    }
    else
        e.rrpv = (num_refs > 1) ? RRIP_MAX : 1;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::cset_type::iterator
__TEMPLATE_CLASS__::get_likely_dead_line(cset_type& s)
{
    return std::find_if(s.begin(), s.end(),
                [] (const auto& x) { return x.likely_dead; });
}

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::cset_type::iterator
__TEMPLATE_CLASS__::get_way_in_lru_pos(cset_type& s)
{
    return std::min_element(s.begin(), s.end(),
                [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::SetDuelingRole
__TEMPLATE_CLASS__::get_set_role(size_t s) const
{
    size_t grp = s >> numeric_traits<LEADER_SETS>::log2,
           off = fast_mod<LEADER_SETS>(s);
    size_t coff = off ^ (LEADER_SETS-1);

    if (grp == off)
        return SetDuelingRole::LEADER_1;
    else if (grp == coff)
        return SetDuelingRole::LEADER_2;
    else
        return SetDuelingRole::FOLLOWER;
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::update_psel(size_t idx)
{
    SetDuelingRole r = get_set_role(idx);
    psel_ += static_cast<int16_t>(r);
    psel_ = std::clamp(psel_, PSEL_MIN, PSEL_MAX);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
