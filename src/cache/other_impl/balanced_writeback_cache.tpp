/* author: Suhas Vittal
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
    __TEMPLATE_PARENT__::tick();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(size_t idx, cset_type& s, const Transaction& trans)
{
    // Check if corresponding write counter is saturated: if so, then evict a
    // clean line:
    auto v_it = s.end();
    
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

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_lru_mod(cset_type& s, const Transaction& trans)
{
    size_t channel = dram_channel(trans.address),
           bank_idx = dram_bank_idx(trans.address);
    
    const auto& ctrs = balance_counters_[channel];
    auto [min_it, max_it] = std::minmax_element(ctrs.begin(), ctrs.end());
    size_t c = ctrs.at(bank_idx);

    bool prio_read = (c - *min_it) >= 1,
         prio_write = (*max_it - c) >= 1;

    // Use normal LRU if there is no need for balancing yet:
    if (!prio_read && !prio_write)
        return __TEMPLATE_PARENT__::repl_lru(s, trans);

    if (bypass_repl(trans, prio_write))
        return s.end();

    auto v_it = check_dead_block_prediction(s, trans, prio_write);

    // If we still failed, use the nuclear option:
    if (v_it == s.end())
    {
        // Compute LRU positions of all entries:
        std::unordered_map<uint64_t, ssize_t> pos_map;
        std::transform(s.begin(), s.end(), std::inserter(pos_map, pos_map.begin()),
                [&s] (const auto& e)
                {
                    ssize_t p = cset_get_lru_position_of_entry(e, s.begin(), s.end());
                    return std::make_pair(e.address, p);
                });
        v_it = std::min_element(s.begin(), s.end(),
                    [&pos_map, prio_read] (const auto& x, const auto& y)
                    {
                        ssize_t px = pos_map[x.address],
                                py = pos_map[y.address];
                        /*
                         *
                        if (x.dirty == y.dirty)
                        {
                            return x.timestamp < y.timestamp;
                        }
                        else if (dw >= 2)
                        {
                            if (std::abs(px-py) <= 3*IMPL::NUM_WAYS/4)
                                return y.dirty;
                            else
                                return x.timestamp < y.timestamp;
                        }
                        else
                        {
                            if (std::abs(px-py) <= IMPL::NUM_WAYS/2)
                                return prio_write ? x.dirty : y.dirty;
                            else
                                return x.timestamp < y.timestamp;
                        }
                        */
                        if (x.dirty != y.dirty)
                        {
                            if (std::abs(px-py) <= IMPL::NUM_WAYS/2)
                                return prio_read ? y.dirty : x.dirty;
                        }
                        return x.timestamp < y.timestamp;
                    });
    }

    return v_it;
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_rrip_mod(cset_type& s, const Transaction& trans)
{
    size_t channel = dram_channel(trans.address),
           bank_idx = dram_bank_idx(trans.address);
    
    bool prio_write = balance_counters_[channel][bank_idx] < MAX_WRITE_COUNTER;
    if (bypass_repl(trans, prio_write))
        return s.end();

    auto v_it = check_dead_block_prediction(s, trans, prio_write);

    // If we failed to find a usable dead block, default to replacement policy:
    if (v_it == s.end())
    {
        v_it = std::min_element(s.begin(), s.end(),
                    [prio_write] (const auto& x, const auto& y)
                    {
                        if (x.dirty == y.dirty)
                            return x.rrpv < y.rrpv;
                        else if (std::abs(x.rrpv-y.rrpv) <= 1)
                            return prio_write ? x.dirty : y.dirty;
                        else
                            return x.rrpv < y.rrpv;
                    });

        // Check for potential bypass:
        auto r = v_it->rrpv;
        for (auto& e : s)
        {
            e.rrpv -= r;
            e.rrpv = std::clamp(e.rrpv, static_cast<int8_t>(0), RRIP_MAX);
        }
    }
    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::bypass_repl(const Transaction& trans, bool prio_write)
{
    // Try bypass using dead block predictor only if the line is dirty/clean according to `prio_write`
    return dbp_->predict_if_dead(trans) && (trans_is_write(trans.type) == prio_write);
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::check_dead_block_prediction(cset_type& s, const Transaction& trans, bool prio_write)
{
    // Do dead block search, same strategy:
    return std::find_if(s.begin(), s.end(),
                        [prio_write] (const auto& e) { return e.likely_dead && (e.dirty == prio_write); });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void 
__TEMPLATE_CLASS__::enqueue_writeback(Transaction trans)
{
    __TEMPLATE_PARENT__::enqueue_writeback(trans);

    size_t ch = dram_channel(trans.address),
           bank_idx = dram_bank_idx(trans.address);
    ++balance_counters_[ch][bank_idx];
    ++total_writebacks_[ch];
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
