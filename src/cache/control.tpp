/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#define __TEMPLATE_HEADER__ template <class IMPL, class CACHE, class NEXT_CONTROL>
#define __TEMPLATE_CLASS__ CacheControl<IMPL, Cache, NEXT_CONTROL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::CacheControl(next_ptr& n)
    :cache_(new CACHE),
    io_(new IOBus(IMPL::RQ_SIZE, IMPL::WQ_SIZE, IMPL::PQ_SIZE)),
    next_(n)
{
    mshr_.reserve(IMPL::NUM_MSHR);
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
    if (mshr_it != mshr_.end()) {
        mshr_it->second.is_fired = next_->io_->add_incoming(mshr_it->second.trans);
    }
    // Now perform cache accesses.
    for (size_t i = 0; i < IMPL::NUM_RW_PORTS; i++)
        next_access();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::mark_load_as_done(uint64_t address)
{
    // First, fill the entry into the cache.
    auto fill_res = cache_->fill(address);
    if (fill_res.has_value()) {
        // Then we evicted some line.
        CacheEntry& e = fill_res.value();
        // Handle dirty lines.
        // Only worry about clean lines if the next cache is invalidate on hit.
        if (e.dirty)
            next_->io_->add_incoming(Transaction(0, 0, TransactionType::WRITE, e.address));
        else if constexpr (IMPL::NEXT_IS_INVALIDATE_ON_HIT)
            next_->cache_->fill(e.address);
    }
    // Now handle MSHR
    auto it = mshr_.end();
    while ((it == mshr_.find(address)) != mshr_.end()) {
        MSHREntry& e = it->second;
        io_->outgoing_queue.emplace(e.trans, IMPL::CACHE_LATENCY);

        if constexpr (IMPL::WRITE_ALLOCATE) {
            if (e.is_for_write_allocate) {
                cache_->mark_dirty(e.address);
                ++s_write_alloc_;
            }
        }

        s_tot_penalty_ += GL_CYCLE - e.cycle_fired;
        ++s_num_penalty_;

        mshr_.erase(it);
    }
    ++s_fills_;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::next_access()
{
    // Now try to issue some access
    if (mshr_.size() == IMPL::NUM_MSHR)
        return;
    trans_t tt = io_->get_next_incoming();
    if (!tt.has_value())
        return;
    Transaction& t = tt.value();
    if (trans_is_read(t.type)) {
        // Probe the cache
        if (cache_->probe(t.address))
            handle_hit(t);
        else
            handle_miss(t);
    } else {
        // Mark the line in the cache as dirty. If `WRITE_ALLOCATE` is
        // specified (i.e. for the L1D$, then on a write miss, install
        // an MSHR entry).
        if constexpr (IMPL::WRITE_ALLOCATE) {
            if (!cache_->mark_dirty(t.address))
                handle_miss(t, true);
        } else {
            cache_->mark_dirty(t.address);
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::handle_hit(const Transaction& t)
{
    // Update `io_`'s outgoing queue.
    io_->outgoing_queue_.emplace(t, GL_CYCLE + IMPL::CACHE_LATENCY);
    if constexpr (IMPL::INVALIDATE_ON_HIT) {
        cache_->invalidate(t.address);
        ++s_invalidate_;
    }
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::handle_miss(const Transaction& t, bool write_miss)
{
    MSHREntry e(t, write_miss);
    e.is_fired = (mshr_.count(t.address) > 0) || next_->io_->add_incoming(t);
    mshr_.insert({t.address, t});
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
