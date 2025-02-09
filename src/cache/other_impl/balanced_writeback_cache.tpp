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
    for (size_t c = 0; c < DRAM_CHANNELS; c++)
    {
        if (total_writebacks_[c] == DRAM_WQ_SIZE)
        {
            // Reset all counters:
            total_writebacks_[c] = 0;
            balance_counters_[c].fill(0);
        }
    }

    __TEMPLATE_PARENT__::tick();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(size_t idx, cset_type& s, const Transaction& trans)
{
    size_t channel = dram_channel(idx),
           bank_idx = dram_bank_idx(idx);
    
    // Check if corresponding write counter is saturated: if so, then evict a
    // clean line:
    auto v_it = s.end();
    if (balance_counters_[channel][bank_idx] >= MAX_WRITE_COUNTER) 
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
    // First, check if the incoming line is a clean, dead block
    bool likely_dead = dbp_->predict_if_dead(trans);
    if (likely_dead && trans_is_read(trans.type))
        return s.end();

    // If not, search for clean dead blocks
    auto v_it = std::find_if(s.begin(), s.end(),
                        [] (const auto& e) { return e.likely_dead && !e.dirty; });

    // If we still failed, use the nuclear option:
    if (v_it == s.end())
    {
        v_it = std::min_element(s.begin(), s.end(),
                    [] (const auto& x, const auto& y)
                    {
                        if (x.dirty == y.dirty)
                            return x.timestamp < y.timestamp;
                        else
                            return y.dirty;
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

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
