/*
 *  author: Suhas Vittal
 *  date:   5 February 2025
 * */

#include "dram/address.h"
#include "util/numerics.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ BalancedWritebackCache<IMPL, NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    // Handle writebacks:
    if (in_write_mode_[curr_channel_])
    {
        ssize_t& bank_idx = wb_bank_idx_[curr_channel_];
        // Get buffer iterators using `d`
        auto b_begin = balance_buffer_.begin() + balance_buffer_index(curr_channel_, bank_idx);
        auto b_end = b_begin + bb_entries_per_bank_;

        auto b_it = std::find_if(b_begin, b_end,
                            [] (const auto& e) { return e.has_value(); });
        if (b_it != b_end)
        {
            issue_writeback_from_balance_buffer(b_it);
            --balance_buffer_occu_;
        }
        fast_increment_and_mod_inplace<DRAM_TOT_BANKS_PER_CHANNEL>(bank_idx);
    }

    if (fast_mod<4096>(psel_counter_) == 4095)
    {
        uint64_t lhs = global_miss_counter_ * base_access_counter_;
        uint64_t rhs = base_miss_counter_ * global_access_counter_;
    
        bool cond_met = lhs <= (rhs + (rhs>>4));

        psel_ = cond_met ? psel_+1 : psel_>>1;
        psel_ = std::clamp(psel_, PSEL_MIN, PSEL_MAX);
        
        base_miss_counter_ >>= 2;
        base_access_counter_ >>= 2;
        global_miss_counter_ >>= 2;
        global_access_counter_ >>= 2;
        
        psel_counter_ = 0;
    }

    fast_increment_and_mod_inplace<DRAM_CHANNELS>(curr_channel_);

    __TEMPLATE_PARENT__::tick();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::probe(const Transaction& trans)
{
    bool hit = __TEMPLATE_PARENT__::probe(trans);
    if (is_basic_set(cache_set_index<IMPL>(trans.address)))
    {
        ++psel_counter_;
        ++base_access_counter_;
        if (!hit)
            ++base_miss_counter_;
    }
    else
    {
        ++global_access_counter_;
        if (!hit)
            ++global_miss_counter_;
    }
    return hit;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(size_t idx, cset_type& s, const Transaction& trans)
{
    // Check if corresponding write counter is saturated: if so, then evict a
    // clean line:
    auto v_it = s.end();
    
    bool use_mod_policy = !is_basic_set(idx) && ((idx & PSEL_MAX) <= 2*psel_);

    if (use_mod_policy) 
    {
        if constexpr (IMPL::REPL == CacheReplPolicy::LRU)
        {
            v_it = repl_lru_mod(s, trans);
        }
        else if constexpr (IMPL::REPL == CacheReplPolicy::RAND)
        {
            v_it = __TEMPLATE_PARENT__::repl_rand(s, trans);
        }
        else if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP)
        {
            v_it = repl_rrip_mod(s, trans);
        }
        else if constexpr (IMPL::REPL == CacheReplPolicy::DRRIP)
        {
            __TEMPLATE_PARENT__::update_psel(idx);
            v_it = repl_rrip_mod(s, trans);
        }
        else
        {
            std::cerr << "unsupported (modified) cache replacement policy.\n";
            exit(1);
        }
    }
    else
    {
        v_it = __TEMPLATE_PARENT__::find_victim(idx, s, trans);
    }

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_lru_mod(cset_type& s, const Transaction& trans)
{
    size_t channel = dram_channel(trans.address),
           bank_idx = dram_bank_idx(trans.address);
    
    bool prio_write = balance_counters_[channel][bank_idx] < MAX_WRITE_COUNTER + bb_entries_per_bank_;

    // Try bypass using dead block predictor only if the line is dirty/clean according to `prio_write`
    bool likely_dead = dbp_->predict_if_dead(trans);
    if (likely_dead && (trans_is_write(trans.type) == prio_write))
        return s.end();

    // Do dead block search, same strategy:
    auto v_it = std::find_if(s.begin(), s.end(),
                        [prio_write] (const auto& e) { return e.likely_dead && (e.dirty == prio_write); });

    // If we still failed, use the nuclear option:

    // Compute LRU positions of all entries:
    std::unordered_map<uint64_t, ssize_t> pos_map;
    std::transform(s.begin(), s.end(), std::inserter(pos_map, pos_map.begin()),
            [&s] (const auto& e)
            {
                ssize_t p = cset_get_lru_position_of_entry(e, s.begin(), s.end());
                return std::make_pair(e.address, p);
            });

    if (v_it == s.end())
    {
        v_it = std::min_element(s.begin(), s.end(),
                    [prio_write, &pos_map] (const auto& x, const auto& y)
                    {
                        if (x.dirty == y.dirty)
                            return x.timestamp < y.timestamp;
                        else
                        {
                            ssize_t px = pos_map[x.address],
                                    py = pos_map[y.address];
                            if (std::abs(px-py) <= 8)
                                return prio_write ? x.dirty : y.dirty;
                            else
                                return x.timestamp < y.timestamp;
                        }
                    });
    }
    return v_it;
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_rrip_mod(cset_type& s, const Transaction& trans)
{
    // First, check if the incoming line is a clean, dead block
    bool likely_dead = dbp_->predict_if_dead(trans);
    if (likely_dead && trans_is_read(trans.type))
        return s.end();

    // If not, search for clean dead blocks
    auto v_it = std::find_if(s.begin(), s.end(),
                        [] (const auto& e) { return e.likely_dead && !e.dirty; });

    return std::min_element(s.begin(), s.end(),
                [] (const auto& x, const auto& y)
                {
                    if (x.dirty == y.dirty)
                        return x.rrpv < y.rrpv;
                    else
                        return y.dirty;
                });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void 
__TEMPLATE_CLASS__::enqueue_writeback(Transaction trans)
{
    pending_writebacks_.insert(trans.address);
    
    size_t ch = dram_channel(trans.address),
           bank_idx = dram_bank_idx(trans.address);

    // Insert writeback to buffer:
    // First, we need to find a location for this writeback:
    size_t start_idx = balance_buffer_index(ch, bank_idx);
    size_t end_idx = start_idx + bb_entries_per_bank_;

    auto begin = balance_buffer_.begin() + start_idx,
         end = balance_buffer_.begin() + end_idx;

    auto it = std::find_if_not(begin, end,
                    [] (const auto& e) { return e.has_value(); });

    if (it == end)
    {
        // Choose a random entry
        it = std::next(begin, std::rand() % bb_entries_per_bank_);
        issue_writeback_from_balance_buffer(it);
    }
    it->emplace(std::move(trans));

    ++balance_buffer_occu_;
    ++balance_counters_[ch][bank_idx];
    ++total_writebacks_[ch];
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
