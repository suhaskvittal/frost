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
__TEMPLATE_CLASS__::warmup_access(uint64_t addr, bool write)
{
    bool hit = write ? cache_->mark_dirty(addr) : cache_->probe(addr);
    if (hit) {
        if constexpr (IMPL::INVALIDATE_ON_HIT)
            cache_->invalidate(addr);
    } else {
        // Handle miss.
        if (write) {
            if constexpr (IMPL::WRITE_ALLOCATE)
                next_->warmup_access(addr, false);
        } else {
            next_->warmup_access(addr, false);
        }
        // Do fill.
        if constexpr (!IMPL::INVALIDATE_ON_HIT) {
            auto res = cache_->fill(addr);
            if (res.has_value()) {
                CacheEntry& e = res.value();
                if constexpr (IMPL::NEXT_IS_INVALIDATE_ON_HIT)
                    next_->cache_->fill(e.address);
                if (e.dirty)
                    next_->warmup_access(addr, true);
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
__TEMPLATE_CLASS__::demand_fill(uint64_t address, bool dirty)
{
    auto fill_res = cache_->fill(address);
    if (dirty)
        cache_->mark_dirty(address);
    if (fill_res.has_value()) {
        // Then we evicted some line.
        CacheEntry& e = fill_res.value();
        if (e.dirty)
            ++s_writebacks_;
        // Install into the next level of the cache.
        if constexpr (IMPL::NEXT_IS_INVALIDATE_ON_HIT)
            next_->demand_fill(e.address, e.dirty);
        else if (e.dirty && !do_writeback(e.address))
            writeback_queue_.push_back(e.address);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::deadlock_find_inst(const iptr_t& inst)
{
    std::cerr << "searching in " << cache_name_ << "...\n";
    if (!io_->deadlock_find_inst(inst)) {
        // Search in mshr.
        auto mshr_it = std::find_if(mshr_.cbegin(), mshr_.cend(),
                                [inst] (const auto& e)
                                {
                                    const Transaction& t = e.second.trans;
                                    return t.contains_inst(inst);
                                });
        if (mshr_it != mshr_.end()) {
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
    if (trans_is_read(t.type)) {
        // Probe the cache
        ++s_accesses_[t.coreid];
        if (cache_->probe(t.address))
            handle_hit(t);
        else
            handle_miss(t);
    } else {
        // If this cache is invalidate on hit, we must fill first.
        if constexpr (IMPL::INVALIDATE_ON_HIT)
            cache_->fill(t.address);
        // Mark the line in the cache as dirty. If `WRITE_ALLOCATE` is
        // specified (i.e. for the L1D$, then on a write miss, install
        // an MSHR entry).
        bool write_hit = cache_->mark_dirty(t.address);
        if constexpr (IMPL::WRITE_ALLOCATE) {
            ++s_accesses_[t.coreid];
            if (!write_hit)
                handle_miss(t, true);
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
    if (write_miss)
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
