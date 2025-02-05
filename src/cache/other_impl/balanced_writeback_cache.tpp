/*
 *  author: Suhas Vittal
 *  date:   5 February 2025
 * */

#include "dram/address.h"
#include "util/numerics.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
#define __TEMPLATE_CLASS__ BalancedWritebackCache<IMPL, NUM_SETS, NUM_WAYS, NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::tick()
{
    // Issue writeback:
    auto& buf_array = balanced_buffer_[issue_to_channel_];
    size_t& wb_ctr = total_writebacks_in_epoch_[issue_to_channel_];

    // First search for any full buffers:
    auto buf_it = std::find_if(buf_array.begin(), buf_array.end(),
                        [] (const auto& buf) { return buf.size() == BANK_BUFFER_SIZE; });

    // If there is no full buffer, find a buffer that can issue writes (not issued full budget to DRAM)
    if (buf_it == buf_array.end())
    {
        buf_it = std::find_if(buf_array.begin(), buf_array.end(),
                        [] (const auto& buf) { return !buf.empty() && buf.epoch_write_counter < MAX_WRITE_COUNTER; });
    }

    if (buf_it != buf_array.end())
    {
        auto& trans = buf_it->back();
        if (next_->can_accept(trans.address, trans.type) && next_->add_incoming(trans))
        {
            pending_writebacks_.erase(trans.address);
            buf_it->pop_back();
            
            // Increment epoch write counter (local and global)
            ++buf_it->epoch_write_counter;
            ++wb_ctr;
        }

        if (wb_ctr == DRAM_WQ_SIZE)
        {
            // Reset all counters:
            wb_ctr = 0;
            for (auto& buf : buf_array)
                buf.epoch_write_counter = 0;
        }
    }
    fast_increment_and_mod_inplace<DRAM_CHANNELS>(issue_to_channel_);

    __TEMPLATE_PARENT__::tick();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::find_victim(cset_type& s)
{
    size_t channel = dram_channel(s[0].address),
           bank_idx = dram_bank_idx(s[0].address);
    
    // Check if corresponding write counter is saturated: if so, then evict a
    // clean line:
    const auto& buf = balanced_buffer_.at(channel).at(bank_idx);
    if (buf.epoch_write_counter >= MAX_WRITE_COUNTER)
    {
        if constexpr (IMPL::REPL == CacheReplPolicy::LRU)
            return lru_mod(s);
        else if constexpr (IMPL::REPL == CacheReplPolicy::RAND)
            return rand(s);
        else if constexpr (IMPL::REPL == CacheReplPolicy::SRRIP)
            return rrip_mod(s);
        else if constexpr (IMPL::REPL == CacheReplPolicy::DRRIP)
        {
            update_psel(set_index(s[0].address));
            return rrip_mod(s);
        }
        else
        {
            std::cerr << "unsupported (modified) cache replacement policy.\n";
            exit(1);
        }
    }
    else
        return __TEMPLATE_PARENT__::find_victim(s);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::lru_mod(cset_type& s)
{
    return std::min_element(s.begin(), s.end(),
                [] (const auto& x, const auto& y)
                {
                    if (x.dirty == y.dirty)
                        return x.timestamp < y.timestamp;
                    else
                        return y.dirty;
                });
}

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::rrip_mod(cset_type& s)
{
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
