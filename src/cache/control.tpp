/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class CACHE, class NEXT_CONTROL>
#define __TEMPLATE_CLASS__ CacheControl<IMPL, CACHE, NEXT_CONTROL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline
__TEMPLATE_CLASS__::CacheControl(std::string cache_name, next_ptr& n)
    :cache_(new CACHE),
    io_(new IOBus(IMPL::RQ_SIZE, IMPL::WQ_SIZE, IMPL::PQ_SIZE)),
    cache_name_(cache_name),
    next_(n)
{
    mshr_.reserve(IMPL::NUM_MSHR);

    if constexpr (IMPL::WRITEBACK_MODE == CacheWbMode::VIRTUAL_WRITE_QUEUE)
        vwq_ = vwq_ptr(new VirtualWriteQueue<CACHE>(cache_));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::warmup_access(uint64_t addr, bool write)
{
    // If the cache is not write-allocate, then writes are pretty easy to handle.
    bool hit;
    if (write) 
    {
        if (IMPL::WRITE_ALLOCATE)
            hit = cache_probe(addr, true);
        else 
        {
            cache_mark(addr, true);
            return;
        }
    } 
    else
        hit = cache_probe(addr);

    if (hit) 
    {
        if constexpr (IMPL::INVALIDATE_ON_HIT)
            cache_->invalidate(addr);
    } 
    else 
    {
        // Handle miss.
        next_->warmup_access(addr, false);
        // Do fill.
        if constexpr (!IMPL::INVALIDATE_ON_HIT) 
        {
            auto res = cache_->fill(addr, 1);
            if (write)
                cache_mark(addr, true);
            if (res.has_value()) 
            {
                CacheEntry& e = res.value();
                if constexpr (IMPL::NEXT_IS_INVALIDATE_ON_HIT)
                    next_->cache_->fill(e.address, 1);
                if (e.dirty)
                    next_->warmup_access(e.address, true);
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    // First try to deal with any MSHR entries that are not fired.
    auto mshr_it = std::find_if_not(mshr_.begin(), mshr_.end(),
                        [] (auto& x)
                        {
                            return x.second.is_fired;
                        });
    if (mshr_it != mshr_.end())
        mshr_it->second.is_fired = next_->io_->add_incoming(mshr_it->second.trans);
    // Now try writebacks
    if (!writeback_queue_.empty())
    {
        uint64_t addr = writeback_queue_.front();
        if (do_writeback(addr))
            writeback_queue_.pop_front();
    }
    // Now perform cache accesses.
    for (size_t i = 0; i < IMPL::NUM_RW_PORTS; i++)
        next_access();

    if constexpr (IMPL::WRITEBACK_MODE == CacheWbMode::VIRTUAL_WRITE_QUEUE)
    {
        vwq_try_switch_write_mode();
        vwq_schedule_writebacks();
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::mark_load_as_done(uint64_t address)
{
    if (!mshr_.count(address)) 
    {
        std::cerr << cache_name_ << ": zombie mshr wakeup for address " << address << "\n";
        exit(1);
    }
    // First, fill the entry into the cache.
    auto [begin, end] = mshr_.equal_range(address);
    if constexpr (!IMPL::INVALIDATE_ON_HIT) 
    {
        size_t refcnt = std::transform_reduce(begin, end, static_cast<size_t>(0),
                                    std::plus<size_t>{},
                                    [] (const auto& x)
                                    {
                                        const MSHREntry& e = x.second;
                                        return e.trans.inst_list.size();
                                    });
        demand_fill(address, refcnt);
    }
    // Now handle MSHR
    for (auto it = begin; it != end; it++) 
    {
        const MSHREntry& e = it->second;

        if (e.is_for_write_allocate) 
        {
            cache_mark(e.trans.address, true);
            ++s_write_alloc_[e.trans.coreid];
        } 
        else
            io_->add_outgoing(e.trans, IMPL::CACHE_LATENCY);

        s_tot_penalty_[e.trans.coreid] += GL_CYCLE - e.cycle_fired;
        ++s_num_penalty_[e.trans.coreid];
    }
    mshr_.erase(begin, end);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::demand_fill(uint64_t address, size_t refcnt, bool dirty)
{
    CACHE::fill_result_t v, w;

    if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::EAGER)
        std::tie(v,w) = cache_->fill_with_eager_writeback(address, refcnt);
    else if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::NEXT_LINE)
        std::tie(v,w) = cache_->fill_with_next_line_writeback(address, refcnt);
    else
        v = cache_->fill(address, refcnt);

    if (dirty)
        cache_mark(address, true);
    if (v.has_value()) 
    {
        // Then we evicted some line.
        CacheEntry& e = v.value();
        if (e.dirty)
        {
            ++s_writebacks_;
            const auto [s_p, it] = (e.address & 1) ? cache_->find(e.address-1) : cache_->find(e.address+1);
            if (it != s_p->end())
            {
                ++s_dirty_victim_adj_lines_;
                if (it->dirty)
                    ++s_dirty_victim_adj_lines_also_dirty_;
            }

            if constexpr (IMPL::WRITEBACK_MODE == CacheWbMode::VIRTUAL_WRITE_QUEUE)
                vwq_->update_criticality(cache_->get_set_idx(e,address));
        }
        // Install into the next level of the cache.
        if constexpr (IMPL::NEXT_IS_INVALIDATE_ON_HIT)
            next_->demand_fill(e.address, 1, e.dirty);
        else if (e.dirty && !do_writeback(e.address))
            writeback_queue_.push_back(e.address);
        // Also writeback `w` if it has a value.
        if (w.has_value())
            handle_eager_writeback(w.value());
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::deadlock_find_inst(const inst_ptr inst)
{
    std::cerr << "searching in " << cache_name_ << "...\n";
    if (!io_->deadlock_find_inst(inst)) 
    {
        // Search in mshr.
        auto mshr_it = std::find_if(mshr_.cbegin(), mshr_.cend(),
                                [inst] (const auto& e)
                                {
                                    const Transaction& t = e.second.trans;
                                    return t.contains_inst(inst);
                                });
        if (mshr_it != mshr_.end()) 
        {
            const MSHREntry& e = mshr_it->second;
            std::cerr << "\tfound in mshr: is_fired = " << e.is_fired
                    << ", cycle_fired = " << e.cycle_fired
                    << ", write_alloc = " << e.is_for_write_allocate
                    << ", address = " << e.trans.address
                    << ", count = " << mshr_.count(e.trans.address)
                    << "\n";
            return true;
        }
        std::cerr << "\tnothing found\n";
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::next_access()
{
    if (curr_mshr_size() == IMPL::NUM_MSHR)
        return;

    // Now try to issue some access
    auto tt = io_->get_next_incoming();
    if (!tt.has_value())
        return;
    Transaction& t = tt.value();
    if (trans_is_read(t.type)) 
    {
        // Probe the cache
        ++s_accesses_[t.coreid];
        if (cache_probe(t.address))
            handle_hit(t);
        else
            handle_miss(t);
    }
    else 
    {
        // Mark the line in the cache as dirty. If `WRITE_ALLOCATE` is
        // specified (i.e. for the L1D$, then on a write miss, install
        // an MSHR entry).
        if constexpr (IMPL::WRITE_ALLOCATE)
        {
            ++s_accesses_[t.coreid];
            if (!cache_probe(t.address, true))
                handle_miss(t, true);
        }
        else
            cache_mark(t.address, true);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_hit(const Transaction& t)
{
    // Update `io_`'s outgoing queue.
    io_->add_outgoing(t, IMPL::CACHE_LATENCY);
    if constexpr (IMPL::INVALIDATE_ON_HIT)
    {
        cache_->invalidate(t.address);
        ++s_invalidates_[t.coreid];
    }
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_miss(const Transaction& t, bool write_miss)
{
    ++s_misses_[t.coreid];

    MSHREntry e(t, write_miss);

    // Need to switch transaction type in case of write allocate.
    if (write_miss)
        e.trans.type = TransactionType::READ;
    e.is_fired = mshr_.count(t.address) > 0 || next_->io_->add_incoming(e.trans);
    mshr_.insert({t.address, e});
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_eager_writeback(CACHE::entry_t& e)
{
    ++s_eager_writebacks_;
    if constexpr (IMPL::LAZY_EARLY_WRITEBACK)
    {
        if (do_writeback(e.address))
            cache_mark(e.address, false);
    }
    else
    {
        cache_mark(e.address, false);
        if (!do_writeback(e.address))
            writeback_queue_.push_back(e.address);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::do_writeback(uint64_t address)
{
    Transaction t(0, nullptr, TransactionType::WRITE, address);
    return next_->io_->add_incoming(t);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::cache_probe(uint64_t address, bool write)
{
    if constexpr (IMPL::WRITEBACK_MODE == CacheWbMode::VIRTUAL_WRITE_QUEUE)
        return vwq_->probe_with_criticality_update(address);
    else
        return cache_->probe(address, write);
}

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::cache_mark(uint64_t address, bool dirty)
{
    if constexpr (IMPL::WRITEBACK_MODE == CacheWbMode::VIRTUAL_WRITE_QUEUE)
        return vwq_->mark_with_criticality_update(address, dirty);
    else
        return cache_->mark(address, dirty);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::vwq_try_switch_write_mode()
{
    for (size_t i = 0; i < next_->channels_.size(); i++)
    {
        auto& ch = next_->channels_[i];
        if (vwq_->get_count(i) >= vwq_->high_watermark_)
            ch->force_toggle_write_mode(vwq_->get_count(i) - vwq_->low_watermark_);
    }
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::vwq_schedule_writebacks()
{
    auto channel_it = std::find_if(next_->channels_.begin(), next_->channels_.end(),
                                [] (const auto& ch)
                                {
                                    return ch->write_queue_size() >= DRAM_LOW_WATERMARK;
                                });
    if (channel_it != next_->channels_.end())
    {
        auto wb = vwq_->schedule_writeback((*channel_it)->channel_id_);
        if (wb.has_value())
        {
            CacheEntry& e = wb.value();
            auto row_hits = vwq_->harvest_write_row_hits(e.address);
            // Perform writebacks.
            next_->io_->add_incoming(e.address);
            for (const auto& f : row_hits)
                next_->io_->add_incoming(f.address);
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class CACHE_TYPE, class DRAIN_CALLBACK> void 
drain_cache_outgoing_queue(std::unique_ptr<CACHE_TYPE>& c, const DRAIN_CALLBACK& handle_drain)
{
    // Need to make sure queue is drained at the appropriate time (hence the second check).
    auto& out_queue = c->io_->outgoing_queue_;
    while (!out_queue.empty())
    {
        auto& [t, cycle_done] = out_queue.top();
        if (GL_CYCLE < cycle_done)
            return;
        handle_drain(t);
        out_queue.pop();
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
