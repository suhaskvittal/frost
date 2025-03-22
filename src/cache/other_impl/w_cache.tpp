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
    false_evict_counters_(num_false_counters(), SEL_INIT),
    false_eager_counters_(num_false_counters(), SEL_INIT),
    sampled_sets_(OPT_WCACHE_SAMPLED_SETS),
    set_modulus_(IMPL::NUM_SETS / OPT_WCACHE_SAMPLED_SETS),
    set_modulus_ilog2_(ilog2(set_modulus_))
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
update_max_pos(const std::vector<int16_t>& ctrs, size_t& mp, int16_t threshold)
{
    auto it = std::find_if(ctrs.begin(), ctrs.end(),
                    [threshold] (auto x) { return x < threshold; });
    mp = std::distance(ctrs.begin(), it);
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    __TEMPLATE_PARENT__::tick();

    if (GL_CYCLE > 10'000'000 && GL_CYCLE % 1'000'000 == 0)
    {
        update_max_pos(false_evict_counters_, max_fill_lookup_pos_base_, SEL_THRESHOLD);
        update_max_pos(false_eager_counters_, max_eager_lookup_pos_base_, SEL_THRESHOLD);

        /*
        std::cout << "fill pos = " << max_fill_lookup_pos_base_ << "\tctrs:";
        for (auto c : false_evict_counters_)
            std::cout << " " << c;

        update_max_pos(false_eager_counters_, max_eager_lookup_pos_base_, SEL_THRESHOLD);

        /*
        std::cout << "\teager pos = " << max_eager_lookup_pos_base_ << "\tctrs:";
        for (auto c : false_eager_counters_)
            std::cout << " " << c;

        std::cout << "\n";
        */
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::channel_request_demand_writeback(size_t channel_id)
{
    if (OPT_WCACHE_DISABLE_LLC_AS_VIRTUAL_BUFFER)
        return;

    size_t rand_idx;
    do
    {
        rand_idx = fast_mod( static_cast<size_t>(std::rand()), IMPL::NUM_SETS );
    }
    while (dram_channel(rand_idx) != channel_id);

    initiate_eager_writeback(rand_idx);
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

    size_t p = get_entry_position(*it, s.begin(), s.end());
    if (it->test_eager_pos >= 0)
    {
        if (p <= it->test_eager_pos || repl_is_rrip_based(IMPL::REPL))
            update_sel(false_eager_counters_[it->test_eager_pos], false, SEL_MIN, SEL_MAX);
        else
            false_eager_counters_[it->test_eager_pos] >>= 1;
    }
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
            e.rrpv = std::min(RRIP_MAX, static_cast<int8_t>(max_fill_lookup_pos_base_));
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
    way_iterator v_it = s.end();

    const size_t m = max_fill_lookup_position(idx);
    bool avoid_writeback = !is_sampled_set(idx) && !OPT_WCACHE_DISABLE_LLC_AS_VIRTUAL_BUFFER;
 
    for (auto it = s.begin(); it != s.end(); it++)
    {
        size_t p = cset_get_lru_position_of_entry(*it, s.begin(), s.end());
        if (p >= m)
            continue;

        if (v_it == s.end() || repl_impl(*it, *v_it, avoid_writeback))
            v_it = it;
    }

    if (is_sampled_set(idx))
    {
        v_it->test_evict_pos = cset_get_lru_position_of_entry(*v_it, s.begin(), s.end());
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
    way_iterator v_it = s.end();

    const size_t m = max_fill_lookup_position(idx);
    bool avoid_writeback = !is_sampled_set(idx) && !OPT_WCACHE_DISABLE_LLC_AS_VIRTUAL_BUFFER;

    // We want to be able to recall the `rrpv` of the evict entry if this is a sampled
    // set.
    std::vector<int8_t> old_rrpvs(IMPL::NUM_WAYS);
    if (is_sampled_set(idx))
    {
        std::transform(s.begin(), s.end(), old_rrpvs.begin(),
                    [] (const auto& e) { return e.rrpv; });
    }

    if (m == 0)
    {
        v_it = std::min_element(s.begin(), s.end(),
                            [] (const auto& x, const auto& y) { return x.rrpv < y.rrpv; });
    }
    else
    {
        do
        {
            for (auto it = s.begin(); it != s.end(); it++)
            {
                if (it->rrpv >= m)
                    continue;

                if (v_it == s.end() || repl_impl(*it, *v_it, avoid_writeback))
                    v_it = it;
            }

            if (v_it == s.end())
            {
                for (auto& e : s)
                    --e.rrpv;
            }
        }
        while (v_it == s.end());
    }
    
    if (is_sampled_set(idx))
    {
        // Restore `rrpv`'s:
        for (size_t i = 0; i < IMPL::NUM_WAYS; i++)
            s[i].rrpv = old_rrpvs[i];
            
        v_it->test_evict_pos = v_it->rrpv;
        v_it = __TEMPLATE_PARENT__::repl_rrip(s, trans);
        handle_victim_from_sampled_set(v_it);
    }
    else
    {
        int8_t r = v_it->rrpv;
        for (auto& e : s)
        {
            e.rrpv -= r;
            if (e.rrpv < 0)
                e.rrpv = 0;
        }
    }

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::repl_impl(const CacheEntry& x, const CacheEntry& y, bool avoid_writeback)
{
    bool cmp;

    if (repl_is_rrip_based(IMPL::REPL))
        cmp = (x.rrpv < y.rrpv);
    else
        cmp = (x.timestamp < y.timestamp);

    bool x_prio = this->has_writeback_priority(x.address),
         y_prio = this->has_writeback_priority(y.address);

    if (x.dirty && y.dirty)
        return ((x_prio == y_prio) && cmp) || ((x_prio != y_prio) && x_prio);
    else if (x.dirty)
        return !avoid_writeback && (x_prio || (cmp && y_prio));
    else if (y.dirty)
        return avoid_writeback || (!y_prio && (cmp || !x_prio));

    return cmp;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::initiate_eager_writeback(size_t idx)
{
    cset_type& s = csets_[idx];
    const size_t m = max_eager_lookup_position(idx);

    auto it = std::find_if(s.begin(), s.end(),
                    [this, m, begin=s.begin(), end=s.end()]
                    (const auto& e)
                    {
                        size_t p = this->get_entry_position(e, begin, end);
                        return e.valid && e.dirty && this->has_writeback_priority(e.address) && p < m;
                    });

    if (it != s.end())
    {
        if (is_sampled_set(idx))
        {
            size_t p = get_entry_position(*it, s.begin(), s.end());
            it->test_eager_pos = p;
        }
        else
        {
            Transaction wb_trans{NUM_THREADS, 0, it->address, nullptr, Transaction::Type::WRITE};
            enqueue_writeback(wb_trans);

            it->dirty = false;

            ++s_writebacks_;
            ++s_eager_writebacks_;
        }
    }
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

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
