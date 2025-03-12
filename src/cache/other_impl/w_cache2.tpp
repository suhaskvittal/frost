/*
 *  author: Suhas Vittal
 *  date:   10 March 2025
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ WCache2<IMPL, NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::WCache2(std::string name, typename __TEMPLATE_PARENT__::next_ptr& n)
    :__TEMPLATE_PARENT__(name, n),
    write_hits_(__TEMPLATE_CLASS__::num_hit_counters(), 0),
    sampled_sets_(OPT_WCACHE_SAMPLED_SETS),
    set_modulus_(IMPL::NUM_SETS / sampled_sets_),
    set_modulus_ilog2_(ilog2(set_modulus_))
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    __TEMPLATE_PARENT__::tick();

    if (GL_CYCLE > 5'000'000 && GL_CYCLE % 5'000'000 == 0)
    {

        // Update buffer size:
        if (OPT_WCACHE_FIXED_LOOKUP_POS < 0)
        {
            auto it = std::find_if_not(write_hits_.rbegin(), write_hits_.rend(), 
                                    [] (auto c) { return c < 4; });
            max_virtual_buffer_size_ = std::distance(write_hits_.rbegin(), it);
        }

        for (auto& c : write_hits_)
            c >>= 1;
    }
}


__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::channel_request_demand_writeback(size_t channel_id)
{
    if (!mshr_has_space())
        return;

    size_t rand_idx = fast_mod( static_cast<size_t>(std::rand()), IMPL::NUM_SETS );
    while (dram_channel(rand_idx) != channel_id || is_sampled_set(rand_idx))
        rand_idx = fast_mod( static_cast<size_t>(std::rand()), IMPL::NUM_SETS );

    cset_type& s = csets_[rand_idx];

    Transaction dummy{};
    repl_lru_w(rand_idx, s, dummy);

    // Search entries in the virtual buffer that also match a bank that needs a writeback:
    auto it = std::find_if(s.begin(), s.end(),
                    [this] (const auto& e)
                    { 
                        return e.valid && e.dirty && e.in_virtual_buffer && this->has_writeback_priority(e.address);
                    });
    if (it != s.end())
    {
        Transaction wb_trans{NUM_THREADS, 0, it->address, nullptr, Transaction::Type::WRITE};
        enqueue_writeback(wb_trans);

        it->dirty = false;

        ++s_writebacks_;
        ++s_eager_writebacks_;

        size_t p = cset_get_lru_position_of_entry(*it, s.begin(), s.end());
        s_tot_eager_pos_ += p;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::child_handle_mark_dirty_hit(cset_type& s, cset_type::iterator it, const Transaction&)
{
    size_t idx = cache_set_index<IMPL>(it->address);
    if (is_sampled_set(idx))
    {
        size_t p;
        if constexpr (repl_is_rrip_based(IMPL::REPL))
        {
            p = RRIP_MAX - it->rrpv;
        }
        else
        {
            p = cset_get_lru_position_of_entry(*it, s.begin(), s.end());
            p = IMPL::NUM_WAYS - p - 1;
        }

        ++write_hits_[p];
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(size_t idx, cset_type& s, const Transaction& trans)
{
    // Check for virtual lines that are not dirty -- assume dead:
    auto v_it = std::find_if(s.begin(), s.end(),
                        [] (const auto& e) { return e.valid && !e.dirty && e.in_virtual_buffer; });
    if (v_it != s.end())
    {
        ++s_dead_block_evictions_;
        return v_it;
    }

    if constexpr (IMPL::REPL == CacheReplPolicy::LRU)
        v_it = repl_lru_w(idx, s, trans);
    else if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP)
        v_it = repl_rrip_w(idx, s, trans);
    else
        v_it = __TEMPLATE_PARENT__::find_victim(idx, s, trans);

    // Update stats:
    if (v_it->dirty)
    {
        if (v_it->in_virtual_buffer)
            ++s_virtual_buffer_evictions_;

        if (has_writeback_priority(v_it->address))
            ++s_priority_evictions_;
    }

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_lru_w(size_t idx, cset_type& s, const Transaction& trans)
{
    if (is_sampled_set(idx))
        return __TEMPLATE_PARENT__::repl_lru(s, trans);

    size_t v_count = std::count_if(s.begin(), s.end(),
                            [] (const auto& e) { return e.valid && e.dirty && e.in_virtual_buffer; });

    size_t max_v_size = OPT_WCACHE_FIXED_LOOKUP_POS < 0 ? max_virtual_buffer_size_ : OPT_WCACHE_FIXED_LOOKUP_POS;

    way_iterator v_it;
    while (true)
    {
        bool evict_virtual = (v_count >= max_v_size) && max_v_size > 0;
        if (evict_virtual)
        {
            v_it = std::min_element(s.begin(), s.end(),
                            [this] (const auto& x, const auto& y)
                            {
                                bool x_prio = x.in_virtual_buffer && this->has_writeback_priority(x.address),
                                     y_prio = y.in_virtual_buffer && this->has_writeback_priority(y.address);

                                return repl_cmp_w(x.timestamp < y.timestamp, x.dirty, y.dirty, x_prio, y_prio);
                            });
        }
        else
        {
            v_it = std::min_element(s.begin(), s.end(),
                            [] (const auto& x, const auto& y)
                            {
                                if (x.in_virtual_buffer == y.in_virtual_buffer)
                                    return x.timestamp < y.timestamp;
                                else
                                    return y.in_virtual_buffer;
                            });
        }
                        
        if (v_it->dirty && !v_it->in_virtual_buffer && v_count < max_v_size)
        {
            v_it->in_virtual_buffer = true;
            ++v_count;
        }
        else
        {
            break;
        }
    }

    // Collect stats about virtual buffer:
    s_tot_virtual_occu_ += v_count;
    s_tot_virtual_capacity_ += max_virtual_buffer_size_; 
    ++s_tot_virtual_stat_samples_;

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_rrip_w(size_t idx, cset_type& s, const Transaction& trans)
{
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
