/*
 *  author: Suhas Vittal
 *  date:   31 January 2025
 * */

#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ Cache<IMPL,NUM_SETS,NUM_WAYS,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::Cache(std::string cache_name, next_ptr& n)
    :cache_name_(cache_name),
    next_(n)
{
    pending_reads_.reserve(IMPL::RQ_SIZE + IMPL::PQ_SIZE);
    pending_writes_.reserve(IMPL::WQ_SIZE);
    pending_misses_.reserve(IMPL::NUM_MSHR);
    pending_writebacks_.reserve(IMPL::WB_QUEUE_SIZE);

    mshr_.reserve(IMPL::NUM_MSHR);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::warmup_access(uint64_t address, bool write)
{
    if constexpr (!IMPL::WRITE_ALLOCATE)
    {
        if (write)
        {
            if (!mark(address, true))
                warmup_fill(address, true);
            return;
        }
    }

    bool hit = probe(address, write);
    if (!hit)
    {
        next_->warmup_access(address, write);
        warmup_fill(address, write);
    }
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::warmup_fill(uint64_t address, bool dirty)
{
    auto out = fill(address, 1, dirty)[0];
    if (out.entry.valid && out.entry.dirty)
        next_->warmup_access(out.entry.address, true);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    // Check for sleeping MSHR entries:
    if (num_mshr_asleep_ > 0)
    {
        auto mshr_it = std::find_if_not(mshr_.begin(), mshr_.end(),
                                [] (const auto& x) { return x.second.is_fired; });
        auto& [address, e] = *mshr_it;
        // Try access to next level:
        if (next_->can_accept(address, e.trans.type) && next_->add_incoming(e.trans))
        {
            e.is_fired = true;
            --num_mshr_asleep_;
        }
    }

    // Send the next writeback to `next_`
    if (!writeback_queue_.empty())
    {
        const auto& trans = writeback_queue_.front();
        if (next_->can_accept(trans.address, trans.type) && next_->add_incoming(trans))
        {
            pending_writebacks_.erase(trans.address);
            writeback_queue_.pop_front();
        }
    }

    for (size_t ii = 0; ii < IMPL::NUM_FILL_PORTS; ii++)
        do_next_fill();

    for (size_t ii = 0; ii < IMPL::NUM_READ_PORTS; ii++)
        do_next_access(true);

    for (size_t ii = 0; ii < IMPL::NUM_WRITE_PORTS; ii++)
        do_next_access(false);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::can_accept(uint64_t, TransactionType t)
{
    if (t == TransactionType::PREFETCH)
        return prefetch_queue_.size() < IMPL::PQ_SIZE;
    else if (t == TransactionType::WRITE)
        return write_queue_.size() < IMPL::WQ_SIZE;
    else
        return read_queue_.size() < IMPL::RQ_SIZE;
}

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::can_accept_fill()
{
    return fill_queue_.size() < IMPL::FILL_QUEUE_SIZE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::add_incoming(Transaction trans)
{
    bool is_read = trans_is_read(trans.type);

    // First, check if we can forward writes:
    if (pending_writes_.count(trans.address) || pending_writebacks_.count(trans.address))
    {
        if (is_read)
            outgoing_queue_.emplace(trans, GL_CYCLE+1);
        return true;
    }

    // Now, if this is a read, check if we can merge with any other reads:
    if (is_read)
    {
        if (pending_reads_.count(trans.address))
        {
            auto rd_it = std::find_if(read_queue_.begin(), read_queue_.end(),
                            [x = trans.address] (const auto& tr) { return tr.address == x; });
            rd_it->merge(trans);
            return true;
        }
        else if (pending_misses_.count(trans.address) && mshr_.size() < IMPL::NUM_MSHR)
        {
            add_mshr_entry(trans);
            return true;
        }
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
                        [inst] (const auto& tr) { return tr.contains_inst(inst); });
    if (rd_it != read_queue_.end())
    {
        size_t rd_pos = std::distance(read_queue_.begin(), rd_it);
        std::cerr << "\tfound instruction in read queue: position = " << rd_pos
                    << ", read queue occupancy = " << read_queue_.size()
                    << ", write_queue occupancy = " << write_queue_.size()
                    << ", prefetch queue occupancy = " << prefetch_queue_.size()
                    << ", mshr occupancy = " << mshr_.size()
                    << ", writeback queue occupancy = " << writeback_queue_.size()
                    << "\n";
        return true;
    }
    
    // Search in MSHR
    auto mshr_it = std::find_if(mshr_.begin(), mshr_.end(),
                        [inst] (const auto& x) { return x.second.trans.contains_inst(inst); });
    if (mshr_it != mshr_.end())
    {
        const auto& [address, e] = *mshr_it;
        std::cerr << "\tfound instruction in mshr: is fired = " << e.is_fired << ", cycle fired = "
            << e.cycle_fired << "\n";
        return true;
    }

    std::cerr << "\tnothing found\n";
    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe(uint64_t address, bool write)
{
    cset_type& s = get_set(address);
    auto it = s.find(address);
    if (it != s.end())
    {
        update_entry(*it);
        it->dirty |= write;
        return true;
    }
    else
        return false;
}

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::mark(uint64_t address, bool dirty)
{
    cset_type& s = get_set(address);
    auto it = s.find(address);
    if (it != s.end())
    {
        it->dirty = dirty;
        return true;
    }
    else
        return false;
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::invalidate(uint64_t address)
{
    cset_type& s = get_set(address);
    auto it = s.find(address);
    if (it != s.end())
        it->valid = false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill(uint64_t address, size_t num_refs, bool dirty)
{
    fill_result_type out;

    cset_type& s = get_set(address);

    // First search for an invalid entry:
    auto it = std::find_if_not(s.begin(), s.end(),
                        [] (const auto& e) { return e.valid; });
    if (it == s.end())
    {
        // Search for a dead block:
        it = std::find_if(s.begin(), s.end(),
                        [] (const auto& e) { return e.likely_dead; });
        // If there is no dead block, then use the replacement policy.
        if (it == s.end())
            it = find_victim(s);
        out = fill_result_type(std::move(*it), 0);
    }
    init_entry(*it, address, num_refs, dirty);
    return multi_fill_result_type{out};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill_with_eager_writeback(uint64_t address, size_t num_refs, bool dirty)
{
    auto out = fill(address, num_refs, dirty);

    // Check if the writeback queue even has space for the writeback:
    if (writeback_queue_.size() < IMPL::WB_QUEUE_SIZE)
    {
        auto& s = get_set(address);
        auto lru_it = get_way_in_lru_pos(s, 0);
        if (lru_it->dirty)
            out.emplace_back(*lru_it, 0);
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::multi_fill_result_type
__TEMPLATE_CLASS__::fill_with_same_set_row_harvest(uint64_t address, size_t num_refs, bool dirty)
{
    multi_fill_result_type out = fill(address, num_refs, dirty);
    const auto& primary_victim = out.front();

    if (primary_victim.entry.valid && primary_victim.entry.dirty)
    {
        auto& s = get_set(address);
        uint64_t row = dram_row(primary_victim.entry.address);

        std::unordered_set<uint64_t> visited;
        while (true)
        {
            auto it = std::find_if(s.begin(), s.end(),
                            [row, &visited] (const auto& e) 
                            { 
                                return e.valid && e.dirty 
                                        && !visited.count(e.address) && row == dram_row(e.address);
                            });
            if (it == s.end())
                break;
            size_t p = get_lru_pos(*it, s);
            out.emplace_back(*it, p);
            visited.insert(it->address);
        }
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(cset_type& s)
{
    if constexpr (IMPL::REPL == CacheReplPolicy::LRU)
        return lru(s);
    else if constexpr (IMPL::REPL == CacheReplPolicy::RAND)
        return rand(s);
    else if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP)
        return rrip(s);
    else if constexpr (IMPL::REPL == CacheReplPolicy::DRRIP)
    {
        update_psel(set_index(s[0].address));
        return rrip(s);
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
    if (mshr_.size() >= IMPL::NUM_MSHR || writeback_queue_.size() >= IMPL::WB_QUEUE_SIZE)
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
                            return !this->pending_reads_.count(tr.address)
                                    && !this->pending_misses_.count(tr.address);
                        });
        if (q_it == q.end())
            return false;
    }

    Transaction trans = std::move(*q_it);
    q.erase(q_it);
    if (do_read)
        pending_reads_.erase(trans.address);
    else
        pending_writes_.erase(trans.address);

    // Perform cache access:
    if (do_read)
    {
        ++s_accesses_[trans.coreid];
        if (probe(trans.address))
            outgoing_queue_.emplace(trans, GL_CYCLE+IMPL::CACHE_LATENCY);
        else
        {
            ++s_misses_[trans.coreid];
            add_mshr_entry(trans);
        }
    }
    else
    {
        if constexpr (IMPL::WRITE_ALLOCATE)
        {
            ++s_accesses_[trans.coreid];
            if (!probe(trans.address, true))
            {
                ++s_misses_[trans.coreid];
                add_mshr_entry(trans);
            }
        }
        else
        {
            if (!mark(trans.address, true))
                fill_queue_.push_back(trans);
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::do_next_fill()
{
    if (fill_queue_.empty())
        return;

    const Transaction& trans = fill_queue_.front();
    const bool is_read = trans_is_read(trans.type);
    
    // Compute reference count for the given transaction:
    auto [begin, end] = mshr_.equal_range(trans.address);
    size_t refcnt = 0;
    if (is_read)
    {
        refcnt = std::transform_reduce(begin, end,
                        static_cast<size_t>(0),
                        std::plus<size_t>{},
                        [] (const auto& x) { return x.second.trans.inst_list.size(); });
    }

    // Handle eviction + any writeback if necessary
    multi_fill_result_type eviction_list;
    if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::NORMAL)
        eviction_list = fill(trans.address, refcnt, !is_read);
    else if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::EAGER)
        eviction_list = fill_with_eager_writeback(trans.address, refcnt, !is_read);
    else if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::SAME_SET_ROW_HARVEST)
        eviction_list = fill_with_same_set_row_harvest(trans.address, refcnt, !is_read);
    else
    {
        std::cerr << cache_name_ << ": unknown writeback mode\n";
        exit(1);
    }
    ++s_fills_[trans.coreid]; 

    for (size_t i = 0; i < eviction_list.size(); i++)
    {
        const auto& e = eviction_list[i].entry;

        // If this is the first entry, it is the actual victim line
        if (i == 0)
        {
            ++s_evictions_;
            if (!e.dirty)
                break;
        }

        ++s_writebacks_;
        if (i > 0)
            ++s_eager_writebacks_;

        Transaction wb_trans(trans.coreid, nullptr, TransactionType::WRITE, e.address);
        // Set DRAM row closure hint if possible:
        if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::SAME_SET_ROW_HARVEST)
        {
            wb_trans.dram_issue_priority = eviction_list.size()-1;
        }
        writeback_queue_.push_back(wb_trans);
        pending_writebacks_.insert(e.address);
        
        // If this is an eager writeback, we may need to do more:
        if (i > 0)
        {
            // Clean the line in the cache:
            mark(e.address, false);

            // Record LRU position of eager writebacks:
            s_ssrh_tot_lru_pos_[i-1] += eviction_list[i].lru_pos;
            ++s_ssrh_num_harvests_[i-1];
        }
    }

    // Update MSHR:
    if (is_read)
    {
        for (auto it = begin; it != end; it++)
        {
            MSHREntry& e = it->second;
            if (e.is_for_write_allocate)
            {
                mark(e.trans.address, true);
                ++s_write_alloc_[e.trans.coreid];
            }
            else
                outgoing_queue_.emplace(std::move(e.trans), GL_CYCLE+IMPL::CACHE_LATENCY);

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
    bool write_miss = trans_is_write(trans.type);
    uint64_t address = trans.address;

    MSHREntry e(std::move(trans), write_miss);
    if (write_miss)  // Need to switch transaction type in the case of a write allocate
        e.trans.type = TransactionType::READ;

    if (mshr_.count(address) == 0)
    {
        e.is_fired = next_->can_accept(address, e.trans.type) && next_->add_incoming(e.trans);
        pending_misses_.insert(address);
        if (!e.is_fired)
            ++num_mshr_asleep_;
    }
    else
        e.is_fired = true;

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
