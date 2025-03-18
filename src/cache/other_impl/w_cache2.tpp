/*
 *  author: Suhas Vittal
 *  date:   7 March 2024
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ WCache2<IMPL,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::WCache2(std::string name, typename __TEMPLATE_PARENT__::next_ptr& n)
    :__TEMPLATE_PARENT__(name, n),
    false_evict_counters_(__TEMPLATE_CLASS__::num_false_evict_counters(), SEL_INIT),
    false_eager_counters_(__TEMPLATE_CLASS__::num_false_evict_counters(), SEL_INIT),
    max_fill_lookup_pos_(__TEMPLATE_CLASS__::initial_max_pos()),
    sampled_sets_(OPT_WCACHE_SAMPLED_SETS),
    set_modulus_(IMPL::NUM_SETS / OPT_WCACHE_SAMPLED_SETS),
    set_modulus_ilog2_(ilog2(set_modulus_))
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    __TEMPLATE_PARENT__::tick();

    if (GL_CYCLE > 10'000'000 && GL_CYCLE % 5'000'000 == 0)
    {
        update_max_pos(false_evict_counters_, max_fill_lookup_pos_, SEL_THRESHOLD);
        update_max_pos(false_eager_counters_, max_eager_lookup_pos_, SEL_THRESHOLD);

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
__TEMPLATE_CLASS__::channel_request_demand_writeback(size_t channel_id)
{
    if (!mshr_has_space())
        return;

    size_t rand_idx = fast_mod( static_cast<size_t>(std::rand()), IMPL::NUM_SETS );
    while (dram_channel(rand_idx) != channel_id)
        rand_idx = fast_mod( static_cast<size_t>(std::rand()), IMPL::NUM_SETS );

    cset_type& s = csets_[rand_idx];
    size_t m = is_sampled_set(rand_idx) ? IMPL::NUM_WAYS : max_eager_lookup_pos_;

    auto it = std::find_if(s.begin(), s.end(),
                    [this, begin=s.begin(), end=s.end(), m] (const auto& e)
                    {
                        size_t p = cset_get_lru_position_of_entry(e, begin, end);
                        return e.valid && e.dirty && p < m && this->has_writeback_priority(e.address);
                    });

    if (it != s.end())
    {
        if (is_sampled_set(rand_idx))
        {
            it->test_eager_pos = cset_get_lru_position_of_entry(*it, s.begin(), s.end());
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

    size_t p = cset_get_lru_position_of_entry(*it, s.begin(), s.end());

    if (it->test_eager_pos >= 0 && it->test_eager_pos != p)
        false_eager_counters_[it->test_eager_pos] >>= 1;
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
                        return this->repl_impl(x, y, m, lru_pos[x.address], lru_pos[y.address]);
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
                        return this->repl_impl(x, y, m, x.rrpv, y.rrpv);
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

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::repl_impl(const CacheEntry& x, const CacheEntry& y, size_t max_pos, size_t x_pos, size_t y_pos)
{
    bool x_free = x_pos < max_pos,
         y_free = y_pos < max_pos;

    bool cmp;
    if constexpr (repl_is_rrip_based(IMPL::REPL))
        cmp = (x.rrpv < y.rrpv);
    else
        cmp = (x.timestamp < y.timestamp);

    if (x_free && y_free)
    {
        bool x_prio = this->has_writeback_priority(x.address),
             y_prio = this->has_writeback_priority(y.address);

        if (x.dirty == y.dirty)  // I really don't know why `==` works instead of `&&`
                                 // My theory is that it has something to do with the set sampling
                                 // procedure.
            return (x_prio == y_prio) ? cmp : x_prio;
        else
            return !x.dirty;
    }

    return cmp;
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
