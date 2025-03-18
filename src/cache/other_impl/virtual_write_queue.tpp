/*
 *  author: Suhas Vittal
 *  date:   4 February 2025
 * */

#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ VirtualWriteQueue<IMPL,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    __TEMPLATE_PARENT__::tick();

    if (GL_CYCLE > 5'000'000 && GL_CYCLE % 5'000'000 == 0)
    {
        if (OPT_VWQ_WAYS < 0)
        {
            std::cout << "\twrite hit counters =";
            for (auto c : write_hits_)
                std::cout << " " << c;

            auto it = std::find_if_not(write_hits_.rbegin(), write_hits_.rend(), 
                                    [] (auto c) { return c < 128; });
            num_vwq_ways_ = std::distance(write_hits_.rbegin(), it);
            std::cout << "num_vwq_ways = " << num_vwq_ways_ << "\n";
            
            for (auto& c : write_hits_)
                c >>= 1;
        }
    }

    if (num_vwq_ways_ == 0)
        return;

    // handle when virtual write queue is too large:
    if (!in_write_mode_ && queue_size_ >= high_watermark())
    {
        in_write_mode_ = true;
        drain_next_it_ = critical_map_.begin();
    }
    else if (in_write_mode_ && queue_size_ < low_watermark())
    {
        in_write_mode_ = false;
    }

    // Issue writebacks to memory controller (need to reach below low watermark)
    if (in_write_mode_ && mshr_has_space())
    {
        if (drain_next_it_ == critical_map_.end())
            drain_next_it_ = critical_map_.begin();

        // Get next available write from the virtual write queue:
        auto& [idx, cnt] = *drain_next_it_;
        cset_type& s = csets_[idx];

        // Search for dirty LRU way:
        auto dirty_it = find_dirty_way(s);
    
        // Enqueue into writeback queue: note that since there is not an evictor, we don't really know
        // which core is causing this, so we set `coreid` to `NUM_THREADS`
        Transaction wb_trans{NUM_THREADS, 0, dirty_it->address, nullptr, Transaction::Type::WRITE};
        enqueue_writeback(wb_trans);

        // Clean the selected dirty way and update `drain_next_it_` and `critical_map_`
        dirty_it->dirty = false;
        --cnt;
        --queue_size_;

        // update criticality:
        if (cnt == 0)
            drain_next_it_ = critical_map_.erase(drain_next_it_);
        else
            ++drain_next_it_;

        demand_next_it_ = drain_next_it_;

        ++s_eager_writebacks_;
        ++s_writebacks_;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::channel_request_demand_writeback(size_t channel_id)
{
    if (in_write_mode_ || !mshr_has_space() || critical_map_.empty())
        return;

    if (demand_next_it_ == critical_map_.end())
        demand_next_it_ = critical_map_.begin();

    // Search for critical set matching channel-id.
    auto it = std::find_if(demand_next_it_, critical_map_.end(),
                        [channel_id] (const auto& p) { return dram_channel(p.first) == channel_id; });
    if (it == critical_map_.end())
    {
        demand_next_it_ = it;
        return;
    }

    auto& [idx, cnt] = *it;
    cset_type& s = csets_[idx];

    // Get dirty way:
    auto dirty_it = find_dirty_way(s);
    if (dirty_it == s.end())
        return;

    // Enqueue into writeback queue:
    Transaction wb_trans{NUM_THREADS, 0, dirty_it->address, nullptr, Transaction::Type::WRITE};
    wb_trans.dram_is_demand_writeback = true;
    enqueue_writeback(wb_trans);

    dirty_it->dirty = false;

    --cnt;
    --queue_size_;
    
    // update criticality:
    if (cnt == 0)
        demand_next_it_ = critical_map_.erase(it);
    else
        demand_next_it_ = std::next(it);

    ++s_eager_writebacks_;
    ++s_writebacks_;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe(const Transaction& trans)
{
    bool hit = __TEMPLATE_PARENT__::probe(trans);

    // Update set criticality if this is a hit:
    if (hit)
        update_criticality_via_count(cache_set_index<IMPL>(trans.address));

    return hit;
}

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::mark_dirty(const Transaction& trans)
{
    bool hit = __TEMPLATE_PARENT__::mark_dirty(trans);

    // Update set criticality if this is a hit:
    if (hit)
        update_criticality_via_count(cache_set_index<IMPL>(trans.address));

    return hit;
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill(const Transaction& trans)
{
    auto out = __TEMPLATE_PARENT__::fill(trans);

    update_criticality_via_count(cache_set_index<IMPL>(trans.address));

    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::child_handle_mark_dirty_hit(cset_type& s, cset_type::iterator it, const Transaction& trans)
{
    size_t p;
    p = cset_get_lru_position_of_entry(*it, s.begin(), s.end());
    p = IMPL::NUM_WAYS - p - 1;

    ++write_hits_[p];
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::enqueue_writeback(Transaction trans)
{
    // Collect cache set and bank info before enqueuing writeback
    [[ maybe_unused ]] size_t idx = cache_set_index<IMPL>(trans.address);
    [[ maybe_unused ]] size_t c = dram_channel(trans.address),
                              b = dram_bank_idx(trans.address),
                              row = dram_row(trans.address);

    __TEMPLATE_PARENT__::enqueue_writeback(std::move(trans));

#if !defined(DRAM_AM_ENABLE_PERMUTATION)
    if constexpr (DRAM_PAGE_POLICY != DRAMPagePolicy::CLOSE)
    {
        // Now, search for row buffer hits in critical sets (up-to 3 lookups).
        for (size_t ii = 0; ii < 3 && mshr_has_space(); ii++)
        {
            auto it = std::find_if(critical_map_.begin(), critical_map_.end(),
                            [idx,c,b] (const auto& p)
                            {
                                return p.first != idx && dram_channel(p.first) == c && dram_bank_idx(p.first) == b;
                            });
            
            // Exit early if found nothing:
            if (it == critical_map_.end())
                break;

            auto& [lookup_idx, cnt] = *it;
            auto& s = csets_[lookup_idx];
            auto dirty_it = std::find_if(s.begin(), s.end(),
                                [&s, c, b, row, n=num_vwq_ways_] (const auto& e)
                                {
                                    return e.dirty
                                            && dram_channel(e.address) == c
                                            && dram_bank_idx(e.address) == b
                                            && dram_row(e.address) == row
                                            && cset_get_lru_position_of_entry(e, s.begin(), s.end()) < n;
                                });
            if (dirty_it != s.end())
            {
                // Enqueue writeback:
                Transaction wb_trans{NUM_THREADS, 0, dirty_it->address, nullptr, Transaction::Type::WRITE};
                __TEMPLATE_PARENT__::enqueue_writeback(wb_trans);

                --cnt;
                --queue_size_;
                if (cnt == 0)
                    critical_map_.erase(it);

                ++s_writebacks_;
                ++s_eager_writebacks_;

                // Clean line:
                dirty_it->dirty = false;
            }
        }
    }
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_dirty_way(cset_type& s)
{
    auto it = std::min_element(s.begin(), s.end(),
                            [] (const auto& x, const auto& y)
                            {
                                if (x.dirty == y.dirty)
                                    return x.timestamp < y.timestamp;
                                else
                                    return x.dirty;
                            });
    if (it->valid && it->dirty)
        return it;
    else
        return s.end();
}

__TEMPLATE_HEADER__ inline size_t
__TEMPLATE_CLASS__::count_dirty_lines_in_vwq_ways(const cset_type& s) const
{
    std::vector<CacheEntry> vwq_ways(num_vwq_ways_);
    std::partial_sort_copy(s.begin(), s.end(), vwq_ways.begin(), vwq_ways.end(),
            [] (const auto& x, const auto& y) 
            { 
                if (x.valid && y.valid)
                    return x.timestamp < y.timestamp;
                else
                    return !x.valid;
            });
    return std::count_if(vwq_ways.begin(), vwq_ways.end(),
                    [] (const auto& e) { return e.valid && e.dirty; });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_criticality_via_count(size_t idx)
{
    auto m_it = critical_map_.find(idx);
    
    size_t c = count_dirty_lines_in_vwq_ways(csets_[idx]);
    if (m_it != critical_map_.end())
    {
        if (c == 0)
        {
            queue_size_ -= m_it->second;
            drain_next_it_ = critical_map_.erase(m_it);
            demand_next_it_ = drain_next_it_;
        }
        else
        {
            if (c > m_it->second)
                queue_size_ += c - m_it->second;
            else
                queue_size_ -= m_it->second - c;

            m_it->second = c;
        }
    }
    else if (c > 0)
    {
        critical_map_[idx] = c;
        queue_size_ += c;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
