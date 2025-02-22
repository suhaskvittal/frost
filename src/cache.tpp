/*
 *  author: Suhas Vittal
 *  date:   31 January 2025
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ Cache<IMPL,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::Cache(std::string cache_name, next_ptr& n)
    :cache_name_(cache_name),
    csets_(IMPL::NUM_SETS, cset_type(IMPL::NUM_WAYS)),
    next_(n),
    dead_block_pred_(new typename IMPL::DEAD_BLOCK_PREDICTOR_TYPE),
    partition_manager_(new typename IMPL::PARTITION_MANAGER_TYPE)
{
    pending_reads_.reserve(IMPL::RQ_SIZE + IMPL::PQ_SIZE);
    pending_writes_.reserve(IMPL::WQ_SIZE);
    pending_misses_.reserve(IMPL::NUM_MSHR);
    pending_writebacks_.reserve(IMPL::NUM_MSHR);

    mshr_.reserve(IMPL::NUM_MSHR);

    // Initialize `partitions_` so each core gets as much as they want (initially)
    partition_.fill(IMPL::NUM_WAYS);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::warmup_access(const Transaction& trans)
{
    if constexpr (!IMPL::WRITE_ALLOCATE)
    {
        if (trans.is_write())
        {
            if (!mark_dirty(trans))
                warmup_fill(trans);
            return;
        }
    }

    bool hit = probe(trans);
    if (!hit)
    {
        next_->warmup_access(trans);
        warmup_fill(trans);
    }
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::warmup_fill(const Transaction& trans)
{
    auto out = fill(trans)[0];
    if (out.entry.valid && out.entry.dirty)
    {
        Transaction wb_trans{trans.coreid, trans.ip, out.entry.address, nullptr, Transaction::Type::WRITE};
        next_->warmup_access(wb_trans);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    // We assume that the LLC can either send a read request or a writeback to `next_`
    // every cycle, not both:
    if (num_mshr_asleep_ > 0)
    {
        auto mshr_it = std::find_if_not(mshr_.begin(), mshr_.end(),
                                [] (const auto& x) { return x.second.is_fired; });
        auto& [address, e] = *mshr_it;
        // Try access to next level:
        if (next_->can_accept(e.trans) && next_->add_incoming(e.trans))
        {
            e.is_fired = true;
            --num_mshr_asleep_;
        }
    } 
    else if (!writeback_queue_.empty())
    {
        const auto& trans = writeback_queue_.front();
        if (next_->can_accept(trans) && next_->add_incoming(trans))
        {
            pending_writebacks_.erase(pending_writebacks_.find(trans.address));
            writeback_queue_.pop_front();
        }
    }

    for (size_t ii = 0; ii < IMPL::NUM_FILL_PORTS; ii++)
        do_next_fill();

    for (size_t ii = 0; ii < IMPL::NUM_READ_PORTS; ii++)
        do_next_access(true);

    for (size_t ii = 0; ii < IMPL::NUM_WRITE_PORTS; ii++)
        do_next_access(false);

    // Update partitioning policy:
    if (GL_CYCLE - partition_manager_->last_update_cycle_ >= OPT_CACHE_PARTITION_UPDATE_CYCLES)
        partition_manager_->update_partition(partition_.begin(), partition_.end());
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::add_incoming(Transaction trans)
{
    bool is_read = trans.is_read();

    // First, check if we can forward writes:
    if ((pending_writes_.find(trans.address) != pending_writes_.end()) 
        || (pending_writebacks_.find(trans.address) != pending_writebacks_.end()))
    {
        ++s_write_forwards_[trans.coreid];
        if (is_read)
            outgoing_queue_.emplace(trans, GL_CYCLE+1);
        return true;
    }

    // otherwise, we need to enqueue normally.
    auto& q =     get_queue_ref(trans.type);
    size_t s =    get_queue_size(trans.type);
    auto& p =     is_read ? pending_reads_ : pending_writes_;
    auto& count = is_read ? s_reads_ : s_writes_;

    if (q.size() == s)
        return false;

    q.push_back(trans);
    p.insert(trans.address);
    ++count[trans.coreid];
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::add_incoming_fill(Transaction trans)
{
    if (fill_queue_.size() >= IMPL::FILL_QUEUE_SIZE)
        return false;

    fill_queue_.push_back(trans);
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::deadlock_find_inst(inst_ptr inst) const
{
    std::cerr << "searching in " << cache_name_ << "...\n";

    // Search in read queue:
    auto rd_it = std::find_if(read_queue_.begin(), read_queue_.end(),
                        [inst] (const auto& tr) { return tr.inst == inst; });
    if (rd_it != read_queue_.end())
    {
        size_t rd_pos = std::distance(read_queue_.begin(), rd_it);
        std::cerr << "\tfound instruction in read queue: position = " << rd_pos
                    << ", read queue occupancy = " << read_queue_.size()
                    << ", write_queue occupancy = " << write_queue_.size()
                    << ", prefetch queue occupancy = " << prefetch_queue_.size()
                    << ", mshr occupancy = " << mshr_.size()
                    << ", sleeping mshr = " << num_mshr_asleep_
                    << ", writeback queue occupancy = " << writeback_queue_.size()
                    << "\n";
        return true;
    }
    
    // Search in MSHR
    auto mshr_it = std::find_if(mshr_.begin(), mshr_.end(),
                        [inst] (const auto& x) { return x.second.trans.inst == inst; });
    if (mshr_it != mshr_.end())
    {
        const auto& [address, e] = *mshr_it;
        std::cerr << "\tfound instruction in mshr: is fired = " << e.is_fired << ", cycle fired = "
            << e.cycle_fired
            << ", read queue occupancy = " << read_queue_.size()
            << ", write_queue occupancy = " << write_queue_.size()
            << ", prefetch queue occupancy = " << prefetch_queue_.size()
            << ", mshr occupancy = " << mshr_.size()
            << ", sleeping mshr = " << num_mshr_asleep_
            << ", writeback queue occupancy = " << writeback_queue_.size()
            << ", fill queue occupancy = " << fill_queue_.size()
            << "\n";
        return true;
    }

    std::cerr << "\tnothing found\n";
    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe(const Transaction& trans)
{
    size_t idx = cache_set_index<IMPL>(trans.address);
    cset_type& s = csets_[idx];

    auto it = cset_find(trans.address, s.begin(), s.end());
    if (it != s.end())
    {
        // Update entry data:
        update_entry(*it);
        it->dirty |= trans.is_write();
        
        // Invoke dead block predictor:
        dead_block_pred_->update_on_probe_or_fill(trans);
        it->likely_dead = dead_block_pred_->predict_if_dead(trans);

        // Update partitioning policies:
        partition_manager_->update_on_access(trans);

        return true;
    }
    else
    {
        return false;
    }
}

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::mark_dirty(const Transaction& trans)
{
    size_t idx = cache_set_index<IMPL>(trans.address);
    cset_type& s = csets_[idx];

    auto it = cset_find(trans.address, s.begin(), s.end());
    if (it != s.end())
    {
        if (it->dirty)
            ++s_rewrites_[trans.coreid];

        it->dirty = true;
        return true;
    }
    else
    {
        return false;
    }
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::invalidate(uint64_t address)
{
    size_t idx = cache_set_index<IMPL>(address);
    cset_type& s = csets_[idx];

    auto it = cset_find(address, s.begin(), s.end());
    if (it != s.end())
        it->valid = false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill(const Transaction& trans)
{
    fill_result_type out;

    size_t idx = cache_set_index<IMPL>(trans.address);
    cset_type& s = csets_[idx];

    // Invoke dead block predictor:
    dead_block_pred_->update_on_probe_or_fill(trans);

    // Update partitioning policies:
    partition_manager_->update_on_fill(trans);

    // First search for an invalid entry:
    auto it = std::find_if_not(s.begin(), s.end(),
                        [] (const auto& e) { return e.valid; });
    if (it == s.end())
    {
        it = find_victim(idx, s, trans);

        // If we have a bypass, return immediately:
        if (it == s.end())
        {
            CacheEntry e;
            init_entry(e, trans);
            out = fill_result_type(e, IMPL::NUM_WAYS);
            return multi_fill_result_type{out};
        }

        // Otherwise, move the contents of the victim to the result output
        out = fill_result_type(std::move(*it), 0);
    }
    init_entry(*it, trans);
    return multi_fill_result_type{out};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill_with_eager_writeback(const Transaction& trans)
{
    auto out = fill(trans);

    // Check if the mshr has space:
    if (mshr_has_space())
    {
        size_t idx = cache_set_index<IMPL>(trans.address);
        auto& s = csets_.at(idx);

        auto lru_it = cset_get_way_in_lru_position(s.begin(), s.end(), 0);
        if (lru_it->dirty)
        {
            out.emplace_back(*lru_it, 0);
            lru_it->dirty = false;
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
    {
        return repl_lru(s, trans);
    }
    else if constexpr (IMPL::REPL == CacheReplPolicy::RAND)
    {
        return repl_rand(s, trans);
    }
    else if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP)
    {
        return repl_rrip(s, trans);
    }
    else if constexpr (IMPL::REPL == CacheReplPolicy::DRRIP)
    {
        set_dueling_arbiter_.update_psel(idx);
        return repl_rrip(s, trans);
    }
    else if constexpr (IMPL::REPL == CacheReplPolicy::LRU_DEAD_BLOCK)
    {
        return repl_lru_dead_block(s, trans);
    }
    else
    {
        std::cerr << "unsupported cache replacement policy.\n";
        exit(1);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::do_next_access(bool do_read)
{
    if (!allow_access())
        return false;

    auto& q = do_read ? (read_queue_.empty() ? prefetch_queue_ : read_queue_)
                      : write_queue_;
    if (q.empty())
        return false;

    auto q_it = q.begin();
    if (!do_read)
    {
        q_it = std::find_if(q.begin(), q.end(),
                        [this] (const auto& tr) 
                        { 
                            // Enforce WAR dependence
                            return this->pending_reads_.find(tr.address) == this->pending_reads_.end();
                        });
        if (q_it == q.end())
            return false;
    }

    Transaction trans = std::move(*q_it);
    q.erase(q_it);

    // Perform cache access:
    if (do_read)
    {
        ++s_accesses_[trans.coreid];
        if (probe(trans))
        {
            outgoing_queue_.emplace(trans, GL_CYCLE+IMPL::CACHE_LATENCY);
        }
        else
        {
            ++s_misses_[trans.coreid];
            add_mshr_entry(trans);
        }
        pending_reads_.erase(pending_reads_.find(trans.address));
    }
    else
    {
        if constexpr (IMPL::WRITE_ALLOCATE)
        {
            ++s_accesses_[trans.coreid];
            if (!probe(trans))
            {
                ++s_misses_[trans.coreid];
                add_mshr_entry(trans);
            }
        }
        else if (!mark_dirty(trans))
        {
            fill_queue_.push_back(trans);
        }
        
        // So, unlike reads, we know that there will always be one write, simply because duplicate
        // writes will always be consumed.
        pending_writes_.erase(pending_writes_.find(trans.address));
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::do_next_fill()
{
    if (!allow_fill())
        return;

    const Transaction& trans = fill_queue_.front();
    const bool is_read = trans.is_read();

    // Handle eviction + any writeback if necessary
    multi_fill_result_type eviction_list;
    if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::NORMAL)
    {
        eviction_list = fill(trans);
    }
    else if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::EAGER)
    {
        eviction_list = fill_with_eager_writeback(trans);
    }
    else
    {
        std::cerr << cache_name_ << ": unknown writeback mode\n";
        exit(1);
    }
    ++s_fills_[trans.coreid]; 

    // Handle writebacks + update evict stats:
    for (size_t i = 0; i < eviction_list.size(); i++)
    {
        const auto& e = eviction_list[i].entry;

        // If this is the first entry, it is the actual victim line
        if (i == 0)
        {
            ++s_evictions_;
            if (e.likely_dead)
                ++s_dead_block_evictions_;
            if (eviction_list[i].lru_pos == IMPL::NUM_WAYS)
                ++s_bypasses_;

            if (!e.dirty)
                break;
        }

        ++s_writebacks_;
        if (i > 0)
            ++s_eager_writebacks_;

        Transaction wb_trans{trans.coreid, trans.ip, e.address, nullptr, Transaction::Type::WRITE};
        enqueue_writeback(wb_trans);
    }

    // Update MSHR -- need to send back all Transactions matching the fill address:
    if (is_read)
    {
        auto [begin, end] = mshr_.equal_range(trans.address);
        for (auto it = begin; it != end; it++)
        {
            MSHREntry& e = it->second;
            if (e.is_for_write_allocate)
            {
                mark_dirty(trans);

                // Update stats:
                ++s_write_alloc_[e.trans.coreid];
            }
            else
            {
                outgoing_queue_.emplace(std::move(e.trans), GL_CYCLE+IMPL::CACHE_LATENCY);
            }

            // Update miss penalty:
            s_tot_miss_penalty_[e.trans.coreid] += GL_CYCLE - e.cycle_fired;
            ++s_num_miss_penalty_[e.trans.coreid];
        }
        mshr_.erase(begin, end);
        pending_misses_.erase(trans.address);
    }

    fill_queue_.pop_front();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::add_mshr_entry(Transaction trans)
{
    bool write_miss = trans.is_write();
    uint64_t address = trans.address;

    MSHREntry e(trans, write_miss);
    if (write_miss)  // Need to switch transaction type in the case of a write allocate
        e.trans.type = Transaction::Type::READ;

    // Check if there is already an MSHR entry for this address. If so, we can mark the entry as fired. If not,
    // we need to issue a read request
    auto mshr_it = pending_misses_.find(address);
    if (mshr_it == pending_misses_.end())
    {
        e.is_fired = next_->can_accept(e.trans) && next_->add_incoming(e.trans);
        pending_misses_.insert(address);
        if (!e.is_fired)
            ++num_mshr_asleep_;
    }
    else
    {
        e.is_fired = true;
    }

    mshr_.insert({address, e});
}


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cache/replacement.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class CACHE_TYPE, class FUNC> void
drain_cache_outgoing_queue(std::unique_ptr<CACHE_TYPE>& c, const FUNC& fn)
{
    while (!c->outgoing_queue_.empty())
    {
        const auto& [trans, cyc] = c->outgoing_queue_.top();
        if (cyc > GL_CYCLE)
            return;
        fn(trans);
        c->outgoing_queue_.pop();
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
