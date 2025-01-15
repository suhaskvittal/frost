/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "cache/other_impl/bank_balanced_cache.h"
#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class CACHE, class NEXT_CONTROL>
#define __TEMPLATE_CLASS__ CacheControl<IMPL, CACHE, NEXT_CONTROL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline
__TEMPLATE_CLASS__::CacheControl(std::string cache_name, next_ptr& n)
    :io_(new IOBus(IMPL::RQ_SIZE, IMPL::WQ_SIZE, IMPL::PQ_SIZE)),
    cache_name_(cache_name),
    next_(n)
{
    if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::NEXT_LINE)
    {
        typename CACHE::index_function_type
            index_func = [] (uint64_t x, size_t sets)
            {
                size_t lower = x & ((1L << dram_lowest_col_bit_index()) - 1);
                size_t upper = (x >> (dram_lowest_col_bit_index()+1)) & ((sets>>1)-1);
                return (upper << (dram_lowest_col_bit_index())) | lower;
            };
        cache_ = cache_ptr(new CACHE(index_func));
    }
    else
        cache_ = cache_ptr(new CACHE);

    mshr_.reserve(IMPL::NUM_MSHR);
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
            hit = cache_->probe(addr, true);
        else 
        {
            cache_->mark(addr, true);
            return;
        }
    } 
    else
        hit = cache_->probe(addr);

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
            auto res = cache_->fill(addr, 1, write);
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
    if (num_mshr_asleep_ > 0)
    {
        auto mshr_it = std::find_if_not(mshr_.begin(), mshr_.end(),
                            [] (auto& x)
                            {
                                return x.second.is_fired;
                            });
        if (mshr_it != mshr_.end())
        {
            auto& [address, entry] = *mshr_it;
            entry.is_fired = next_->io_->can_accept(address, entry.trans.type)
                                        && next_->io_->add_incoming(entry.trans);
            if (entry.is_fired)
                --num_mshr_asleep_;
        }
    }
    // Now try writebacks
    if (!writeback_queue_.empty())
    {
        const auto& e = writeback_queue_.front();
        bool success;
        if (e.dram_write_hint_valid)
            success = do_writeback_with_dram_write_hint(e.address, e.dram_write_hint_do_autopre);
        else
            success = do_writeback(e.address);

        if (success)
            writeback_queue_.pop_front();
    }
    // Now perform cache accesses.
    for (size_t i = 0; i < IMPL::NUM_RW_PORTS; i++)
        next_access();

    if (!eager_queue_.empty())
    {
        uint64_t address = eager_queue_.front();
        bool success;
        if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::NEXT_LINE)
            // We know that this should be the last row hit.
            success = do_writeback_with_dram_write_hint(address, true);
        else
            success = do_writeback(address);

        if (success)
            eager_queue_.pop_front();
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
            cache_->mark(e.trans.address, true);
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
    typename CACHE::fill_result_type v, w;
    size_t w_lru_pos;

    if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::EAGER)
        std::tie(v, w) = cache_->fill_with_eager_writeback(address, refcnt, dirty);
    else if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::NEXT_LINE)
        std::tie(v, w, w_lru_pos) = 
            cache_->fill_with_next_line_writeback(address, dram_lowest_col_bit_index(), refcnt, dirty);
    else
        v = cache_->fill(address, refcnt, dirty);

    if (!v.has_value()) 
        return;
    // Then we evicted some line.
    const CacheEntry& e = v.value();
    if (e.dirty)
    {
        ++s_writebacks_;
        const auto [s_p, it] = cache_->find(dram_get_first_column_neighbor(e.address));
        if (it != s_p->end())
        {
            ++s_dirty_victim_adj_lines_;
            if (it->dirty)
                ++s_dirty_victim_adj_lines_also_dirty_;
        }

        if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::NEXT_LINE)
        {
            WBQueueEntry wbqe(e.address);
            wbqe.dram_write_hint_valid = true;
            wbqe.dram_write_hint_do_autopre = (w_lru_pos != 0) || !w.has_value();
            if (!do_writeback_with_dram_write_hint(e.address, wbqe.dram_write_hint_do_autopre))
                writeback_queue_.push_back(wbqe);
        }
        else
        {
            if (!do_writeback(e.address))
                writeback_queue_.emplace_back(e.address);
        }
    }
    else if constexpr (IMPL::NEXT_IS_INVALIDATE_ON_HIT)
        next_->demand_fill(e.address, 1, e.dirty);
    // Also writeback `w` if it has a value.
    if (w.has_value())
    {
        if constexpr (IMPL::WRITEBACK_MODE == CacheWBMode::NEXT_LINE)
        {
            if (w_lru_pos == 0)  // Only writeback if this line will be in the LRU position.
                handle_eager_writeback(w.value());
            s_tot_next_line_lru_pos_ += w_lru_pos;
            ++s_tot_next_lines_;
        }
        else
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

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::sig_dram_write_drain(size_t channel_id, size_t writes_per_bank)
{
    if constexpr (is_bank_balanced_cache<CACHE>::value)
        cache_->decrement_write_counters(channel_id, writes_per_bank);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::next_access()
{
    if (curr_mshr_size() >= IMPL::NUM_MSHR)
        return;

    // Now try to issue some access
    auto tt = io_->get_next_available_request();
    if (!tt.has_value())
        return;
    Transaction& t = tt.value();
    if (trans_is_read(t.type)) 
    {
        // Probe the cache
        ++s_accesses_[t.coreid];
        if (cache_->probe(t.address))
            handle_hit(std::move(t));
        else
            handle_miss(std::move(t));
    }
    else 
    {
        // Mark the line in the cache as dirty. If `WRITE_ALLOCATE` is
        // specified (i.e. for the L1D$, then on a write miss, install
        // an MSHR entry).
        if constexpr (IMPL::WRITE_ALLOCATE)
        {
            ++s_accesses_[t.coreid];
            if (!cache_->probe(t.address, true))
                handle_miss(std::move(t), true);
        }
        else
        {
            if (!cache_->mark(t.address, true))
                demand_fill(t.address, 1, true);  // Can occur if the cache is non-inclusive.
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_hit(Transaction&& t)
{
    // Update `io_`'s outgoing queue.
    if constexpr (IMPL::INVALIDATE_ON_HIT)
    {
        cache_->invalidate(t.address);
        ++s_invalidates_[t.coreid];
    }
    io_->add_outgoing(std::move(t), IMPL::CACHE_LATENCY);
}

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_miss(Transaction&& t, bool write_miss)
{
    ++s_misses_[t.coreid];

    uint64_t address = t.address;
    MSHREntry e(std::move(t), write_miss);

    // Need to switch transaction type in case of write allocate.
    if (write_miss)
        e.trans.type = TransactionType::READ;
    e.is_fired = mshr_.count(address) > 0 
                  || (next_->io_->can_accept(address, e.trans.type) && next_->io_->add_incoming(e.trans));
    if (!e.is_fired)
        ++num_mshr_asleep_;
    mshr_.insert({address, e});
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_eager_writeback(const CacheEntry& e)
{
    if (eager_queue_.size() >= EAGER_QUEUE_SIZE)
        return;

    eager_queue_.push_back(e.address);
    cache_->mark(e.address, false);
    ++s_eager_writebacks_;
    ++s_writebacks_;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::do_writeback(uint64_t address)
{
    if (next_->io_->can_accept(address, TransactionType::WRITE))
    {
        Transaction t(0, nullptr, TransactionType::WRITE, address);
        next_->io_->add_incoming(t);
        return true;
    }
    else
        return false;
}

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::do_writeback_with_dram_write_hint(uint64_t address, bool autopre)
{
    if (next_->io_->can_accept(address, TransactionType::WRITE))
    {
        Transaction t(0, nullptr, TransactionType::WRITE, address);
        t.dram_write_hint_valid = true;
        t.dram_write_hint_do_autopre = autopre;
        next_->io_->add_incoming(t);
        return true;
    }
    else
        return false;
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
