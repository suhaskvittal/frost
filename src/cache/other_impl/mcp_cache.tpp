/*
 *  author: Suhas Vittal
 *  date:   3 March 2025
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ MCPCache<IMPL, NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    __TEMPLATE_PARENT__::tick();

    const size_t high_wm = high_watermark();
    const size_t low_wm = low_watermark();

    // if the high watermark == 0, then we have no victim buffer (full set usage by workloads)
    if (high_wm == 0)
        return;

    // if the high watermark == 0, then we have no victim buffer (full set usage by workloads)
    if (high_wm == 0)
        return;

    if (!in_write_mode_ && total_v_count_ >= high_wm)
    {
        in_write_mode_ = true;
        next_it_ = critical_map_.begin();
    }
    else if (in_write_mode_ && total_v_count_ < low_wm)
    {
        in_write_mode_ = false;
    }

    if (in_write_mode_ && mshr_has_space())
    {
        size_t idx = fast_mod(static_cast<size_t>(std::rand()), IMPL::NUM_SETS);
        ssize_t tries = 2048;
        while (!critical_map_.count(idx) && tries--)
            idx = fast_mod(static_cast<size_t>(std::rand()), IMPL::NUM_SETS);

        if (tries < 0)
            return;

        // Generate a victim -- if it is in the virtual buffer then queue it for writeback:
        Transaction fake_trans{};
        cset_type& s = csets_[idx];

        auto v_it = repl_lru_mcp(idx, s, fake_trans, true);

        if (v_it->dirty && v_it->in_virtual_buffer)
        {
            Transaction wb_trans{NUM_THREADS, 0, v_it->address, nullptr, Transaction::Type::WRITE};
            enqueue_writeback(wb_trans);
            
            // Invalidate line:
            v_it->dirty = false;

            ++s_eager_writebacks_;
            ++s_writebacks_;
        }

        update_criticality_via_count(idx);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::channel_request_demand_writeback(size_t channel_id)
{
    if (in_write_mode_ || !mshr_has_space() || critical_map_.empty())
        return;

    size_t idx = fast_mod(static_cast<size_t>(std::rand()), IMPL::NUM_SETS);
    ssize_t tries = 2048;
    while (dram_channel(idx) != channel_id && tries--)
        idx = fast_mod(static_cast<size_t>(std::rand()), IMPL::NUM_SETS);

    if (tries < 0)
        return;

    // Generate a victim -- if it is in the virtual buffer then queue it for writeback:
    Transaction fake_trans{};
    cset_type& s = csets_[idx];

    auto v_it = repl_lru_mcp(idx, s, fake_trans, false);

    if (v_it->dirty && v_it->in_virtual_buffer)
    {
        Transaction wb_trans{NUM_THREADS, 0, v_it->address, nullptr, Transaction::Type::WRITE};
        wb_trans.dram_is_demand_writeback = true;
        enqueue_writeback(wb_trans);
        
        // Invalidate line:
        v_it->dirty = false;

        ++s_eager_writebacks_;
        ++s_writebacks_;
    }

    update_criticality_via_count(idx);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe(const Transaction& trans)
{
    bool hit = __TEMPLATE_PARENT__::probe(trans);
    if (hit)
        update_criticality_via_count(cache_set_index<IMPL>(trans.address));
    return hit;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::mark_dirty(const Transaction& trans)
{
    bool hit = __TEMPLATE_PARENT__::mark_dirty(trans);
    if (hit)
        update_criticality_via_count(cache_set_index<IMPL>(trans.address));
    return hit;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill(const Transaction& trans)
{
    auto out = __TEMPLATE_PARENT__::fill(trans);

    update_criticality_via_count(cache_set_index<IMPL>(trans.address));

    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(size_t idx, cset_type& s, const Transaction& trans)
{
    way_iterator v_it;

    v_it = std::find_if(s.begin(), s.end(),
                    [] (const auto& e) { return !e.dirty && e.in_virtual_buffer; });

    if (v_it == s.end())
    {
        if constexpr (IMPL::REPL == CacheReplPolicy::LRU)
            v_it = repl_lru_mcp(idx, s, trans, false);
        else
            v_it = __TEMPLATE_PARENT__::find_victim(idx, s, trans);
    }

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_lru_mcp(size_t idx, cset_type& s, const Transaction& trans, bool stop_early)
{
    auto v_it = s.end();

    // Check size of virtual buffer and number of allocations to the buffer:
    const size_t max_v_ways = mcp_manager()->get_victim_part();
    size_t v_count = critical_map_.count(idx) ? critical_map_[idx] : 0;

    // Spin until we find a clean, non-virtual line, or a dirty, virtual line.
    //
    // Move dirty non-virtual victim lines to the virtual buffer
    while (true)
    {
        bool evict_virtual = (v_count > max_v_ways) || stop_early;
        v_it = std::min_element(s.begin(), s.end(),
                        [evict_virtual] (const auto& x, const auto& y)
                        {
                            bool vx = x.in_virtual_buffer && x.dirty,
                                 vy = y.in_virtual_buffer && y.dirty;

                            if (vx == vy)
                                return x.timestamp < y.timestamp;
                            else
                                return evict_virtual == vx;
                        });

        if (v_it->dirty && !v_it->in_virtual_buffer)
        { 
            v_it->in_virtual_buffer = true;
            ++v_count;
        }
        else
        {
            break;
        }
    }

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_criticality_via_count(size_t idx)
{
    const auto& s = csets_.at(idx);
    size_t v_count = std::count_if(s.begin(), s.end(),
                            [] (const auto& e) { return e.valid && e.dirty && e.in_virtual_buffer; });
    auto m_it = critical_map_.find(idx);

    if (m_it != critical_map_.end())
    {
        if (v_count == 0)
        {
            total_v_count_ -= m_it->second;
            next_it_ = critical_map_.erase(m_it);
        }
        else if (v_count != m_it->second)
        {
            if (v_count > m_it->second)
                total_v_count_ += v_count - m_it->second;
            else
                total_v_count_ -= m_it->second - v_count;
            m_it->second = v_count;
        }
    }
    else if (v_count > 0)
    {
        critical_map_.insert({idx, v_count});
        total_v_count_ += v_count;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_CLASS__
#undef __TEMPLATE_HEADER__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
