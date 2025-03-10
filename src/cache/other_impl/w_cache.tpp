/*
 *  author: Suhas Vittal
 *  date:   7 March 2024
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ WCache<IMPL,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::WCache(std::string name, typename __TEMPLATE_PARENT__::next_ptr& n)
    :__TEMPLATE_PARENT__(name, n),
    false_evict_counters_(__TEMPLATE_CLASS__::num_false_evict_counters(), SEL_INIT),
    max_fill_lookup_pos_(__TEMPLATE_CLASS__::initial_max_pos()),
    sampled_sets_(OPT_WCACHE_SAMPLED_SETS),
    set_modulus_(IMPL::NUM_SETS / OPT_WCACHE_SAMPLED_SETS),
    set_modulus_ilog2_(ilog2(set_modulus_))
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
update_max_pos(const std::vector<int16_t>& ctrs, size_t& mp)
{
    auto it = std::find_if(ctrs.begin(), ctrs.end(),
                    [] (auto x) { return x < 128; });
    mp = std::distance(ctrs.begin(), it);
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    __TEMPLATE_PARENT__::tick();

    if (GL_CYCLE > 10'000'000 && GL_CYCLE % 1'000'000 == 0)
    {
        update_max_pos(false_evict_counters_, max_fill_lookup_pos_);

        /*
        std::cout << "fill pos = " << max_fill_lookup_pos_ << "\tctrs:";
        for (auto c : false_evict_counters_)
            std::cout << " " << c;

        std::cout << "\teager pos = " << max_eager_lookup_pos_ << "\tctrs:";
        for (auto c : false_eager_counters_)
            std::cout << " " << c;
        std::cout << "\n";
        */
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::child_handle_probe_hit(cset_type& s, cset_type::iterator it, const Transaction&)
{
    if (it->test_evict_pos >= 0)
        update_sel(false_evict_counters_[it->test_evict_pos], false, SEL_MIN, SEL_MAX);
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::child_handle_mark_dirty_hit(cset_type& s, cset_type::iterator it, const Transaction& trans)
{
    // Same exact logic used:
    child_handle_probe_hit(s, it, trans);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::init_entry(CacheEntry& e, const Transaction& trans)
{
    __TEMPLATE_PARENT__::init_entry(e, trans);

    if constexpr (repl_is_rrip_based(IMPL::REPL))
    {
        size_t idx = cache_set_index<IMPL>(trans.address);
        if (e.rrpv == 1 && !is_sampled_set(idx))
            e.rrpv = std::min(RRIP_MAX, static_cast<int8_t>(max_fill_lookup_pos_));
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(size_t idx, cset_type& s, const Transaction& trans)
{
    if constexpr (IMPL::REPL == CacheReplPolicy::LRU)
        return repl_lru_w(idx, s, trans);
    else if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP)
        return repl_rrip_w(idx, s, trans);
    else
        return __TEMPLATE_PARENT__::find_victim(idx, s, trans);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_lru_w(size_t idx, cset_type& s, const Transaction& trans)
{
    // Compute max LRU position we can look at:
    std::unordered_map<uint64_t, size_t> lru_pos;
    lru_pos.reserve(IMPL::NUM_WAYS);
    std::transform(s.begin(), s.end(), std::inserter(lru_pos, lru_pos.begin()),
            [begin=s.begin(), end=s.end()] (const auto& e)
            {
                size_t p = cset_get_lru_position_of_entry(e, begin, end);
                return std::make_pair(e.address, p);
            });

    const size_t m = max_position_to_use_for_writeback_priority(idx);

    auto v_it = std::min_element(s.begin(), s.end(),
                    [this, m, &lru_pos] (const auto& x, const auto& y)
                    {
                        bool x_prio = lru_pos.at(x.address) < m 
                                            && this->has_writeback_priority(x.address),
                             y_prio = lru_pos.at(y.address) < m
                                            && this->has_writeback_priority(y.address);

                        return repl_cmp_w(x.timestamp < y.timestamp, x.dirty, y.dirty, x_prio, y_prio);
                    });

    if (is_sampled_set(idx))
    {
        // We are not going to use `v_it` as the victim, but we will mark it as a test eviction:
        v_it->test_evict_pos = lru_pos[v_it->address];

        v_it = __TEMPLATE_PARENT__::repl_lru(s, trans);
        handle_victim_from_sampled_set(v_it);
    }

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_rrip_w(size_t idx, cset_type& s, const Transaction& trans)
{
    const size_t m = max_position_to_use_for_writeback_priority(idx);

    auto v_it = std::min_element(s.begin(), s.end(),
                    [this, m] (const auto& x, const auto& y)
                    {
                        bool x_prio = x.rrpv < m && this->has_writeback_priority(x.address),
                             y_prio = y.rrpv < m && this->has_writeback_priority(y.address);

                        return repl_cmp_w(x.rrpv < y.rrpv, x.dirty, y.dirty, x_prio, y_prio);
                    });

    if (is_sampled_set(idx))
    {
        v_it->test_evict_pos = v_it->rrpv;

        v_it = __TEMPLATE_PARENT__::repl_rrip(s, trans);
        handle_victim_from_sampled_set(v_it);
    }

    // Update rrpv values:
    int8_t r = v_it->rrpv;
    for (auto& e : s)
    {
        e.rrpv -= r;
        e.rrpv = std::clamp(e.rrpv, static_cast<int8_t>(0), RRIP_MAX);
    }

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::enqueue_writeback(Transaction trans)
{
    __TEMPLATE_PARENT__::enqueue_writeback(trans);

    // Update bit vector:
    size_t c = dram_channel(trans.address),
           b = dram_bank_idx(trans.address);

    auto& bits = channels_[c].writeback_done;
    bits.set(b);
    if (bits.all())
        bits.reset();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool
repl_cmp_w(bool x_is_older, bool x_d, bool y_d, bool x_p, bool y_p)
{
    /*
     * Result matrix:
     *  ------------------------- x dirty && y dirty ---------------------------------
     *      x_p    y_p    out
     *       n      n     age
     *       y      n      x
     *       n      y      y
     *       y      y     age
     *  ------------------------- x dirty && y clean ---------------------------------
     *      x_p    y_p       out
     *       n      n         y
     *       y      n         x
     *       n      y        age
     *       y      y         x
     *  ------------------------- x clean && y dirty ---------------------------------
     *      x_p    y_p       out
     *       n      n         x
     *       y      n        age
     *       n      y         y
     *       y      y         y
     * */
    if (x_d && y_d)
        return ((x_p == y_p) && x_is_older) || ((x_p != y_p) && x_p);
    else if (x_d)
        return x_p || (x_is_older && y_p);
    else if (y_d)
        return !y_p && (x_is_older || !x_p);
    else
        return x_is_older;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
