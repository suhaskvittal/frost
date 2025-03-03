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
__TEMPLATE_CLASS__::channel_enter_write_mode(size_t channel_id)
{
    write_mode_[channel_id] = true;
    mcp_manager()->toggle_write_mode(channel_id, true);
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::channel_exit_write_mode(size_t channel_id)
{
    write_mode_[channel_id] = false;
    mcp_manager()->toggle_write_mode(channel_id, false);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill(const Transaction& trans)
{
    auto out = __TEMPLATE_PARENT__::fill(trans);

    if (!out.empty() && out[0].entry.valid && out[0].entry.dirty && out[0].entry.in_virtual_buffer)
    {
        // Check if channel is currently draining writes:
        size_t channel = dram_channel(out[0].entry.address);
        if (write_mode_[channel])
        {
            // Flush entire virtual buffer:
            cset_type& s = csets_[cache_set_index<IMPL>(out[0].entry.address)];
            for (auto& e : s)
            {
                if (e.valid && e.dirty && e.in_virtual_buffer)
                {
                    out.emplace_back(std::move(e), 0);  // Don't care about LRU position

                    // Invalidate entry:
                    e.valid = false;
                    e.dirty = false;
                    e.in_virtual_buffer = false;
                }
            }
        }
    }

    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(size_t idx, cset_type& s, const Transaction& trans)
{
    if constexpr (IMPL::REPL == CacheReplPolicy::LRU)
        return repl_lru_mcp(idx, s, trans);
    else
        return __TEMPLATE_PARENT__::find_victim(idx, s, trans);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_lru_mcp(size_t idx, cset_type& s, const Transaction& trans)
{
    size_t channel = dram_channel(idx);

    auto v_it = s.end();

    if (write_mode_[channel])
    {
        // Prioritize virtual buffer entries no matter what:
        v_it = std::find_if(s.begin(), s.end(),
                            [] (const auto& e) { return e.in_virtual_buffer; });
        if (v_it == s.end())
        {
            // Get LRU victim:
            v_it = std::min_element(s.begin(), s.end(),
                            [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
        }
    }
    else
    {
        // Check size of virtual buffer and number of allocations to the buffer:
        const size_t max_v_ways = mcp_manager()->get_victim_part();
        size_t v_count = std::count_if(s.begin(), s.end(),
                            [] (const auto& e) { return e.valid && e.in_virtual_buffer; });

        // Spin until we find a clean, non-virtual line, or a dirty, virtual line.
        //
        // Move dirty non-virtual victim lines to the virtual buffer
        while (true)
        {
            v_it = std::min_element(s.begin(), s.end(),
                            [evict_virtual=(v_count >= max_v_ways)]
                            (const auto& x, const auto& y)
                            {
                                if (x.in_virtual_buffer == y.in_virtual_buffer)
                                    return x.timestamp < y.timestamp;
                                else
                                    return evict_virtual == x.in_virtual_buffer;
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
    }

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_CLASS__
#undef __TEMPLATE_HEADER__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
