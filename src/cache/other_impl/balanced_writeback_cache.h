/*
 *  author: Suhas Vittal
 *  date:   3 February 2025
 * */

#ifndef CACHE_OTHER_IMPL_BALANCED_WRITEBACK_CACHE_h
#define CACHE_OTHER_IMPL_BALANCED_WRITEBACK_CACHE_h

#include "constants.h"
#include "cache.h"
#include "dram/address.h"

#include <algorithm>
#include <array>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<IMPL,NUM_SETS,NUM_WAYS,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
class BalancedWritebackCache : public __TEMPLATE_PARENT__
{
public:
protected:
    struct wb_buffer_type : std::vector<Transaction>
    {
        size_t write_counter =0;
    };

    using wb_buffer_array = std::array<std::array<wb_buffer_type, DRAM_TOT_BANKS_PER_CHANNEL>, DRAM_CHANNELS>;
    using wb_counter_array = std::array<size_t, DRAM_CHANNELS>;

    constexpr static size_t BANK_BUFFER_SIZE = IMPL::WB_QUEUE_SIZE / (DRAM_CHANNELS*DRAM_TOT_BANKS_PER_CHANNEL);
    constexpr static size_t MAX_WRITE_COUNTER = DRAM_WQ_SIZE / DRAM_TOT_BANKS_PER_CHANNEL;
    /*
     * These structures are used to buffer writes and determine when to use the modified replacement policy
     * (i.e., see `lru_mod` and `rrip_mod` below)
     * */
    wb_buffer_array balanced_buffer_{};
    wb_counter_array total_writebacks_{};
    size_t issue_to_channel_ =0;

    using __TEMPLATE_PARENT__::next_;
    using __TEMPLATE_PARENT__::pending_writebacks_;
    using __TEMPLATE_PARENT__::mshr_;
    using __TEMPLATE_PARENT__::fill_queue_;
public:
    using __TEMPLATE_PARENT__::Cache;
    using typename __TEMPLATE_PARENT__::cset_type;
    using typename __TEMPLATE_PARENT__::way_iterator;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    using typename __TEMPLATE_PARENT__::multi_fill_result_type;

    void tick(void) override;
protected:
    way_iterator find_victim(cset_type&) override;

    way_iterator lru_mod(cset_type&);
    way_iterator rrip_mod(cset_type&);

    inline bool no_buffers_are_full(void)
    {
        return std::all_of(balanced_buffer_.begin(), balanced_buffer_.end(),
                        [] (const auto& buf_array)
                        {
                            return std::none_of(buf_array.begin(), buf_array.end(),
                                            [] (const auto& b) { return b.size() >= BANK_BUFFER_SIZE; });
                        });
    }

    inline bool allow_access(void) override
    {
        return mshr_.size() < IMPL::NUM_MSHR && no_buffers_are_full();
    }

    inline bool allow_fill(void) override
    {
        return !fill_queue_.empty() && no_buffers_are_full();
    }

    inline void enqueue_writeback(Transaction trans) override
    {
        size_t channel = dram_channel(trans.address),
               bank_idx = dram_bank_idx(trans.address);
        balanced_buffer_[channel][bank_idx].push_back(trans);

        pending_writebacks_.insert(trans.address);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "balanced_writeback_cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_BALANCED_WRITEBACK_CACHE_h
