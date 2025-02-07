/*
 *  author: Suhas Vittal
 *  date:   31 January 2025
 * */

#include <cstdlib>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::update_entry(CacheEntry& e)
{
    e.timestamp = GL_CYCLE;
    e.rrpv = RRIP_MAX;

    if (e.dirty)
        e.reused_after_marked_dirty = true;
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::init_entry(CacheEntry& e, uint64_t address, size_t num_refs, bool mark_dirty)
{
    e.valid = true;
    e.dirty = mark_dirty;
    e.address = address;
    e.timestamp = GL_CYCLE;
    e.reused_after_marked_dirty = false;
    e.likely_dead = false;

    if constexpr (IMPL::REPL == CacheReplPolicy::DRRIP)
    {
        if (num_refs > 1)
            e.rrpv = RRIP_MAX;
        else
        {
            // Check whether or not to use BRRIP.
            size_t idx = cache_set_index<NUM_SETS>(address);
            SetDuelingRole r = get_set_role(idx);
            // Resolve `r` if it is a follower set.
            if (r == SetDuelingRole::FOLLOWER)
                r = (psel_ & PSEL_MSB_MASK) ? SetDuelingRole::LEADER_2 : SetDuelingRole::LEADER_1;
            if (r == SetDuelingRole::LEADER_1)
            {
                e.rrpv = 1;
                ++s_dueling_pol1_installs_;
            }
            else
            {
                e.rrpv = (bimodal_counter_ == 32) ? 1 : 0;
                ++s_dueling_pol2_installs_;
                fast_increment_and_mod_inplace<32>(bimodal_counter_);
            }
        }
    }
    else
        e.rrpv = (num_refs > 1) ? RRIP_MAX : 1;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::lru(cset_type& s, const Transaction&)
{
    return cset_get_way_in_lru_position(s.begin(), s.end(), 0);
}

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::rand(cset_type& s, const Transaction&)
{
    return std::next(s.begin(), fast_mod<NUM_WAYS>(std::rand()));
}

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::rrip(cset_type& s, const Transaction&)
{
    auto v_it = std::min_element(s.begin(), s.end(),
                        [] (const auto& x, const auto& y) { return x.rrpv < y.rrpv; });
    for (auto& x : s)
        x.rrpv -= v_it->rrpv;
    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::lru_dead_block(cset_type& s, const Transaction& trans)
{
    auto v_it = s.end();

    // Perform a prediction on the fill address:
    bool fill_is_likely_dead = dbp_->predict_if_dead(trans);
    if (fill_is_likely_dead)  // if so, do bypass:
        return v_it;
    
    // Otherwise, check for a dead block:
    v_it = std::find_if(s.begin(), s.end(), 
                [] (const auto& e) { return e.likely_dead; });

    // If no dead block exists, use LRU:
    if (v_it == s.end())
        v_it = lru(s, trans);

    return v_it;
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
__TEMPLATE_CLASS__::update_psel(size_t s)
{
    constexpr static psel_type PSEL_MAX = (1<<PSEL_WIDTH)-1;
    constexpr static psel_type PSEL_MIN = 0;

    SetDuelingRole r = get_set_role(s);
    psel_ += static_cast<psel_type>(r);
    psel_ = std::clamp(psel_, PSEL_MIN, PSEL_MAX);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
