/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#define __TEMPLATE_HEADER__ template <class IMPL, class CACHE, class NEXT_CONTROL>
#define __TEMPLATE_CLASS__ CacheControl<IMPL, CACHE, NEXT_CONTROL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::CacheControl(std::string cache_name, next_ptr& n)
    :cache_(new CACHE),
    io_(new IOBus(IMPL::RQ_SIZE, IMPL::WQ_SIZE, IMPL::PQ_SIZE)),
    cache_name_(cache_name),
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
    if (mshr_it != mshr_.end())
        mshr_it->second.is_fired = next_->io_->add_incoming(mshr_it->second.trans);
    // Now try writebacks
    if (!writeback_queue_.empty()) {
        uint64_t addr = writeback_queue_.front();
        if (do_writeback(addr))
            writeback_queue_.pop_front();
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
    if (!mshr_.count(address)) {
        std::cerr << cache_name_ << ": zombie mshr wakeup for address " << address << "\n";
        exit(1);
    }
    // First, fill the entry into the cache.
    if constexpr (!IMPL::INVALIDATE_ON_HIT)
        demand_fill(address);
    // Now handle MSHR
    auto it = mshr_.end();
    while ((it=mshr_.find(address)) != mshr_.end()) {
        MSHREntry& e = it->second;
        if (e.is_for_write_allocate) {
            cache_->mark_dirty(e.trans.address);
            ++s_write_alloc_[e.trans.coreid];
        } else {
            io_->add_outgoing(e.trans, IMPL::CACHE_LATENCY);
        }

        s_tot_penalty_[e.trans.coreid] += GL_CYCLE - e.cycle_fired;
        ++s_num_penalty_[e.trans.coreid];

        mshr_.erase(it);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::demand_fill(uint64_t address)
{
    auto fill_res = cache_->fill(address);
    if (fill_res.has_value()) {
        // Then we evicted some line.
        CacheEntry& e = fill_res.value();
        // Handle dirty lines.
        // Only worry about clean lines if the next cache is invalidate on hit.
        if (e.dirty) {
            if (!do_writeback(e.address))
                writeback_queue_.push_back(e.address);
        } else if constexpr (IMPL::NEXT_IS_INVALIDATE_ON_HIT) {
            next_->demand_fill(e.address);
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::deadlock_find_inst(const iptr_t& inst)
{
    std::cerr << "searching in " << cache_name_ << "...";
    if (!io_->deadlock_find_inst(inst)) {
        // Search in mshr.
        auto mshr_it = std::find_if(mshr_.cbegin(), mshr_.cend(),
                                [inst] (const auto& e)
                                {
                                    const Transaction& t = e.second.trans;
                                    return t.inst == inst;
                                });
        if (mshr_it != mshr_.end()) {
            const MSHREntry& e = mshr_it->second;
            std::cerr << "\n\tfound in mshr: is_fired = " << e.is_fired
                    << ", cycle_fired = " << e.cycle_fired << "\n";
            return true;
        }
        std::cerr << " nothing found";
    }
    std::cerr << "\n";
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
    if (trans_is_read(t.type)) {
        // Probe the cache
        ++s_accesses_[t.coreid];
        if (cache_->probe(t.address))
            handle_hit(t);
        else
            handle_miss(t);
    } else {
        // Mark the line in the cache as dirty. If `WRITE_ALLOCATE` is
        // specified (i.e. for the L1D$, then on a write miss, install
        // an MSHR entry).
        if constexpr (IMPL::WRITE_ALLOCATE) {
            ++s_accesses_[t.coreid];
            if (!cache_->mark_dirty(t.address))
                handle_miss(t, true);
        } else {
            cache_->mark_dirty(t.address);
        }
        io_->add_outgoing(t, 0);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::handle_hit(const Transaction& t)
{
    // Update `io_`'s outgoing queue.
    io_->add_outgoing(t, IMPL::CACHE_LATENCY);
    if constexpr (IMPL::INVALIDATE_ON_HIT) {
        cache_->invalidate(t.address);
        ++s_invalidates_[t.coreid];
    }
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::handle_miss(const Transaction& t, bool write_miss)
{
    ++s_misses_[t.coreid];

    MSHREntry e(t, write_miss);

    // Need to switch transaction type in case of write allocate.
    e.trans.type = TransactionType::READ;

    e.is_fired = mshr_.count(t.address) > 0 || next_->io_->add_incoming(e.trans);
    mshr_.insert({t.address, e});
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
