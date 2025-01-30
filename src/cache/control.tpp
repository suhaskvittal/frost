/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "cache/dead_block/all.h"
#include "cache/other_impl/all.h"
#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class CACHE_TYPE, class NEXT_CONTROL, class DEAD_BLOCK_PREDICTOR_TYPE>
#define __TEMPLATE_CLASS__ CacheControl<IMPL, CACHE_TYPE, NEXT_CONTROL, DEAD_BLOCK_PREDICTOR_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline
__TEMPLATE_CLASS__::CacheControl(std::string cache_name, next_ptr& n)
    :io_(new IOBus(IMPL::RQ_SIZE, IMPL::WQ_SIZE, IMPL::PQ_SIZE)),
    cache_name_(cache_name),
    cache_(new CACHE_TYPE),
    next_(n),
    dead_block_pred_(new DEAD_BLOCK_PREDICTOR_TYPE)
{
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

    if (!hit) 
    {
        // Handle miss.
        next_->warmup_access(addr, false);
        // Do fill.
        auto res = cache_->fill(addr, 1, write);
        if (res.has_value()) 
        {
            CacheEntry& e = res.value();
            if (e.dirty)
                next_->warmup_access(e.address, true);
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

    if constexpr (is_bank_balanced_cache<CACHE_TYPE>::value)
    {
        if (fast_mod<CACHE_TYPE::COUNTER_REDUCE_CYCLES>(GL_CYCLE) == 0)
            cache_->reduce_hit_rate_counters();
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
    size_t refcnt = std::transform_reduce(begin, end, static_cast<size_t>(0),
                                std::plus<size_t>{},
                                [] (const auto& x)
                                {
                                    const MSHREntry& e = x.second;
                                    return e.trans.inst_list.size();
                                });
    
    // Try and use DBP to bypass the cache:
    const Transaction& front_trans = begin->second.trans;
    DeadBlockPrediction p = dead_block_handle_fill(front_trans);
    if (p != DeadBlockPrediction::LIKELY_DEAD || cache_->fill_will_replace_invalid_victim(address))
    {
        demand_fill(address, refcnt);
        consume_dead_block_prediction(address, p);
    }
    else
        ++s_bypasses_;

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
    typename CACHE_TYPE::fill_result_type v, w;
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

    ++s_evictions_;
    if (e.likely_dead)
        ++s_evictions_due_to_dead_block_predictor_;
    if (e.dirty)
    {
        ++s_writebacks_;
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
__TEMPLATE_CLASS__::deadlock_find_inst(const inst_ptr inst) const
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
    if constexpr (is_bank_balanced_cache<CACHE_TYPE>::value)
        cache_->handle_dram_write_drain(channel_id, writes_per_bank);
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
            // Update dead block predictor:
            // On a writeback miss, forward the line to the MC.
            if (!cache_->mark(t.address, true))
                handle_writeback_miss(std::move(t));
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline void
__TEMPLATE_CLASS__::handle_hit(Transaction&& t)
{
    // Update dead block predictor:
    dead_block_handle_hit(t);

    // Update `io_`'s outgoing queue.
    io_->add_outgoing(std::move(t), IMPL::CACHE_LATENCY);
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::handle_miss(Transaction&& t, bool write_miss)
{
    ++s_misses_[t.coreid];

    uint64_t address = t.address;
    MSHREntry e(std::move(t), write_miss);

    // Need to switch transaction type in case of write allocate.
    if (write_miss)
        e.trans.type = TransactionType::READ;

    if (mshr_.count(address) == 0)
    {
        if constexpr (is_bank_balanced_cache<CACHE_TYPE>::value)
            cache_->handle_mshr_init(address);
        e.is_fired = (next_->io_->can_accept(address, e.trans.type) && next_->io_->add_incoming(e.trans));
    }
    else
        e.is_fired = true;

    if (!e.is_fired)
        ++num_mshr_asleep_;

    mshr_.insert({address, e});
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::handle_writeback_miss(Transaction&& t)
{
    DeadBlockPrediction p = dead_block_handle_fill(t);
    bool do_fill = (p != DeadBlockPrediction::LIKELY_DEAD) 
                    || cache_->fill_will_replace_invalid_victim(t.address);
    
    // Update `do_fill` depending on the cache type:
    if constexpr (is_bank_balanced_cache<CACHE_TYPE>::value)
    {
        auto b = cache_->get_write_balance_level(t.address);
        switch (b)
        {
        case CACHE_TYPE::BalanceLevel::REPL_CLEAN:
            // strong preference towards a fill
            do_fill = (p != DeadBlockPrediction::LIKELY_DEAD)
                        || cache_->fill_will_replace_noncritical_victim(t.address, b);
            break;
        case CACHE_TYPE::BalanceLevel::REPL_DIRTY:
            // strong preference towards a bypass
            do_fill = (p == DeadBlockPrediction::LIKELY_ALIVE)
                        || cache_->fill_will_replace_invalid_victim(t.address); 
            break;
        }
    }

    if (do_fill)
    {
        demand_fill(t.address, 1, true);
        consume_dead_block_prediction(t.address, p);
    }
    else
    {
        ++s_writebacks_;
        ++s_writeback_bypasses_;
        if (!do_writeback(t.address))
            writeback_queue_.emplace_back(t.address);

        if constexpr (is_bank_balanced_cache<CACHE_TYPE>::value)
            cache_->handle_write_bypass(t.address);
    }
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

__TEMPLATE_HEADER__ DeadBlockPrediction
__TEMPLATE_CLASS__::dead_block_handle_fill(const Transaction& trans)
{
#if defined(TRACE_FORMAT_MTF)
    return DeadBlockPrediction::UNSURE;
#endif
    if (dead_block_early_exit(trans))
        return DeadBlockPrediction::UNSURE;

    uint64_t ip = trans.get_front_ip();
    uint8_t coreid = trans.coreid;
    bool writeback = (trans.type == TransactionType::WRITE);
    
    dead_block_pred_->update_on_access(ip, trans.address, coreid, writeback);
    return dead_block_pred_->predict(ip, trans.address, coreid, writeback);
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::dead_block_handle_hit(const Transaction& trans)
{
#if defined(TRACE_FORMAT_MTF)
    return;
#endif
    if (dead_block_early_exit(trans))
        return;

    uint64_t ip = trans.get_front_ip();
    uint8_t coreid = trans.coreid;
    bool writeback = (trans.type == TransactionType::WRITE);

    dead_block_pred_->update_on_access(ip, trans.address, coreid, writeback);
    DeadBlockPrediction p = dead_block_pred_->predict(ip, trans.address, coreid, writeback);
    consume_dead_block_prediction(trans.address, p);
}

__TEMPLATE_HEADER__ inline bool
__TEMPLATE_CLASS__::dead_block_early_exit(const Transaction& trans)
{
    if (trans.type != TransactionType::READ && trans.type != TransactionType::WRITE)
        return true;

    bool override = false;
    // Check if we should override the dead block predictor:
    if constexpr (is_bank_balanced_cache<CACHE_TYPE>::value)
        override = cache_->should_override_dead_block_predictor();
    return override;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::consume_dead_block_prediction(uint64_t address, DeadBlockPrediction p)
{
    switch (p)
    {
    case DeadBlockPrediction::LIKELY_ALIVE:
        cache_->mark_likely_alive(address);
        break;
    case DeadBlockPrediction::LIKELY_DEAD:
        cache_->mark_likely_dead(address);
        ++s_dead_block_predicts_;
        break;
    default:
        break;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class CACHE_TYPE, class DRAIN_CALLBACK> void 
drain_cache_outgoing_queue(std::unique_ptr<CACHE_TYPE>& c, const DRAIN_CALLBACK& handle_drain)
{
    // Need to make sure queue is drained at the appropriate time (hence the second check).
    auto& out_queue = c->io_->outgoing_queue_;
    if (out_queue.count(GL_CYCLE) > 0)
    {
        auto [begin, end] = out_queue.equal_range(GL_CYCLE);
        for (auto it = begin; it != end; it++)
            handle_drain(it->second);
        out_queue.erase(begin, end);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
