/*
 *  author: Suhas Vittal
 *  date:   1 January 2024
 * */

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class CACHE_TYPE>
#define __TEMPLATE_CLASS__  VirtualWriteQueue<CACHE_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::VirtualWriteQueue(cache_ptr& c)
    :high_watermark_(c->num_sets() >> 3),
    low_watermark_(high_watermark_ - DRAM_CHANNELS*DRAM_WQ_SIZE),
    cache_(c)
{
    // Initialize common map:
    for (size_t i = 0; i < cache_->num_sets(); i++)
    {
        dram_resource_tuple_t key = make_resource_key(i);
        dram_common_map_[key].push_back(i);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe_with_criticality_update(uint64_t address)
{
    size_t idx = cache_->get_set_idx(address);
    const auto& s = cache_->csets_.at(idx);
    
    auto it = std::find_if(s.begin(), s.end(),
                        [address] (const auto& e)
                        {
                            return e.valid && e.address == address;
                        });
    if (it == s.end())
        return false;
    if (it->dirty)
        set_critical_count(idx, count_dirty_lru_ways(s));
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::mark_with_criticality_update(uint64_t address, bool dirty)
{
    size_t idx = cache_->get_set_idx(address);
    auto& s = cache_->csets_.at(idx);
    
    auto it = std::find_if(s.begin(), s.end(),
                        [address] (const auto& e)
                        {
                            return e.valid && e.address == address;
                        });
    if (it == s.end())
        return false;
    bool was_dirty = it->dirty;
    it->dirty = dirty;
    count_dirty_lru_ways(s);
    // Now perform the criticality check + update.
    if (was_dirty && !dirty)
        set_critical_count(idx, count_dirty_lru_ways(s));
    else if (!was_dirty && dirty)
    {
        // Check how far this line is from the LRU position.
        size_t pos = std::count_if(s.begin(), s.end(),
                            [it] (const auto& e)
                            {
                                return it->timestamp >= e.timestamp;
                            });
        if (pos < lru_ways())
            increment_critical_count(idx);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::update_criticality(size_t idx)
{
    const auto& s = cache_->csets_.at(idx);
    set_critical_count(idx, count_dirty_lru_ways(s));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::writeback_t
__TEMPLATE_CLASS__::schedule_writeback(size_t channel_idx)
{
    writeback_t wb;
    
    auto it = std::find_if(critical_counts_.begin(), critical_counts_.end(),
                        [channel_idx] (const auto& p)
                        {
                            const auto& [idx, cnt] = p;
                            return dram_channel(idx) == channel_idx && cnt > 0;
                        });
    if (it != critical_counts_.end())
    {
        const auto& [idx, cnt] = *it;
        const auto& s = cache_->csets_.at(idx);
        lru_ways_t w = get_lru_ways(s);
        auto e_it = std::find_if(w.begin(), w.end(),
                        [] (const auto& e)
                        {
                            return e.valid && e.dirty;
                        });
        update_criticality_after_scheduled_writeback(idx, e_it->address);
        wb = *e_it;
    }
    return wb;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::write_hit_probe_t
__TEMPLATE_CLASS__::harvest_write_row_hits(uint64_t base_address)
{
    constexpr size_t MAX_LOOKUPS = 3;

    write_hit_probe_t out;
    out.reserve(MAX_LOOKUPS);

    size_t idx = cache_->get_set_idx(base_address);
    size_t row = dram_row(base_address);

    dram_resource_tuple_t key = make_resource_key(base_address);
    size_t lookups = 0;
    for (size_t iidx : dram_common_map_.at(key))
    {
        if (iidx == idx || critical_counts_[iidx] == 0)
            continue;
        // Search for row buffer hits in the existing set. This counts as a lookup.
        const auto& s = cache_->csets_.at(iidx);
        lru_ways_t w = get_lru_ways(s);
        auto it = std::find_if(w.begin(), w.end(),
                        [row] (const auto& e)
                        {
                            return e.valid && e.dirty && dram_row(e.address) == row;
                        });
        if (it != w.end())
        {
            out.push_back(*it);
            update_criticality_after_scheduled_writeback(iidx, it->address);
        }
        ++lookups;
        if (lookups >= MAX_LOOKUPS)
            break;
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::set_critical_count(size_t idx, size_t to)
{
    size_t& cnt = critical_counts_[idx];
    int64_t diff = static_cast<int64_t>(to) - static_cast<int64_t>(cnt);
    cnt = to;
    dirty_count_ += diff;
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::increment_critical_count(size_t idx)
{
    ++critical_counts_[idx];
    ++dirty_count_;
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::decrement_critical_count(size_t idx)
{
    --critical_counts_[idx];
    --dirty_count_;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::try_switch_write_mode()
{
    if (writes_to_drain_ == 0 && dirty_count_ >= high_watermark_)
    {
        writes_to_drain_ = dirty_count_ - low_watermark_;
        if (writes_to_drain_ > 10000)
        {
            std::cerr << "writes to drain = " << writes_to_drain_ << ", cnt = " << dirty_count_ << ", wm = " << high_watermark_ << ":" << low_watermark_ << "\n";
            exit(1);
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::update_criticality_after_scheduled_writeback(size_t idx, uint64_t address)
{
    decrement_critical_count(idx);
    cache_->mark(address, false);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::lru_ways_t
__TEMPLATE_CLASS__::get_lru_ways(const typename CACHE_TYPE::cset_t& s)
{
    lru_ways_t w(lru_ways());
    std::partial_sort_copy(s.begin(), s.end(), w.begin(), w.end(),
            [] (const auto& x, const auto& y)
            {
                return !y.valid || (x.valid && x.timestamp < y.timestamp);
            });
    return w;
}

__TEMPLATE_HEADER__ inline size_t
__TEMPLATE_CLASS__::count_dirty_lru_ways(const typename CACHE_TYPE::cset_t& s)
{
    lru_ways_t w = get_lru_ways(s);
    return std::count_if(w.begin(), w.end(),
                [] (const auto& e)
                {
                    return e.valid && e.dirty;
                });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
