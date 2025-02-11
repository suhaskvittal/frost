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
#include <optional>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<IMPL,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, class NEXT_TYPE>
class BalancedWritebackCache : public __TEMPLATE_PARENT__
{
public:
protected:
    using local_counter_subarray = std::array<ssize_t, DRAM_TOT_BANKS_PER_CHANNEL>;
    using local_counter_array = std::array<local_counter_subarray, DRAM_CHANNELS>;
    using global_counter_array = std::array<ssize_t, DRAM_CHANNELS>;
    using bool_array = std::array<bool, DRAM_CHANNELS>;

    constexpr static size_t TOT_BANKS = DRAM_TOT_BANKS_PER_CHANNEL * DRAM_CHANNELS;
    constexpr static size_t MAX_WRITE_COUNTER = DRAM_WQ_SIZE / DRAM_TOT_BANKS_PER_CHANNEL;
    /*
     * The buffer definition depends on `IMPL::WB_QUEUE_SIZE` and `TOT_DRAM_BANKS`.
     * */
    using buffer_entry = std::optional<Transaction>;
    using buffer_type = std::vector<buffer_entry>;

    constexpr static size_t PSEL_WIDTH = 3;
    constexpr static int8_t PSEL_MIN = 0;
    constexpr static int8_t PSEL_MAX = (1<<PSEL_WIDTH)-1;
    constexpr static int8_t PSEL_THRESHOLD = (1<<(PSEL_WIDTH-1));
    /*
     * These structures are used to buffer writes and determine when to use the modified replacement policy
     * (i.e., see `lru_mod` and `rrip_mod` below)
     * */
    local_counter_array balance_counters_{};
    global_counter_array total_writebacks_{};

    buffer_type balance_buffer_{};
    size_t balance_buffer_occu_ =0;
    /*
     * Writeback scheduling:
     * */
    global_counter_array wb_bank_idx_{};
    bool_array in_write_mode_{};

    size_t curr_channel_ =0;

    size_t base_access_counter_ =0;
    size_t base_miss_counter_ = 0;
    size_t global_access_counter_ =0;
    size_t global_miss_counter_ =0;
    size_t psel_counter_ =0;

    int8_t psel_ =PSEL_THRESHOLD;
    /*
     * Constant variables:`
     * */
    const size_t bb_entries_per_bank_;

    using __TEMPLATE_PARENT__::next_;
    using __TEMPLATE_PARENT__::dbp_;
    using __TEMPLATE_PARENT__::mshr_;
    using __TEMPLATE_PARENT__::writeback_queue_;
    using __TEMPLATE_PARENT__::pending_writebacks_;
public:
    using write_counts_array = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;

    BalancedWritebackCache(std::string name, typename __TEMPLATE_PARENT__::next_ptr& n)
        :__TEMPLATE_PARENT__(name, n),
        balance_buffer_(IMPL::WB_QUEUE_SIZE),
        bb_entries_per_bank_(std::max(static_cast<size_t>(1), static_cast<size_t>(IMPL::WB_QUEUE_SIZE / TOT_BANKS)))
    {}

    using __TEMPLATE_PARENT__::Cache;
    using typename __TEMPLATE_PARENT__::way_iterator;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    using typename __TEMPLATE_PARENT__::multi_fill_result_type;

    void tick(void) override;

    inline void end_write_mode(size_t channel, const write_counts_array& writes)
    {
        ssize_t& g = total_writebacks_[channel];

        for (size_t i = 0; i < writes.size(); i++)
        {
            ssize_t& c = balance_counters_[channel][i];
            c -= writes.at(i);
            g -= writes.at(i);

            c = std::clamp(c, static_cast<ssize_t>(0), std::numeric_limits<ssize_t>::max());
        }
        g = std::clamp(g, static_cast<ssize_t>(0), std::numeric_limits<ssize_t>::max());
        in_write_mode_[channel] = false;
    }

    inline void start_write_mode(size_t channel)
    {
        in_write_mode_[channel] = true;
    }

    using __TEMPLATE_PARENT__::write_occu;
protected:
    bool probe(const Transaction&) override;
    
    way_iterator find_victim(size_t idx, cset_type&, const Transaction&) override;

    way_iterator repl_lru_mod(cset_type&, const Transaction&);
    way_iterator repl_rrip_mod(cset_type&, const Transaction&);

    void enqueue_writeback(Transaction) override;

    inline void issue_writeback_from_balance_buffer(typename buffer_type::iterator it)
    {
        // Need to issue this writeback -- use `writeback_queue_` for simplicity (already
        // issues writebacks in base class). In practice, there would be no writeback queue:
        writeback_queue_.push_back(std::move(it->value()));
        it->reset();

        --balance_buffer_occu_;
    }

    inline size_t balance_buffer_index(size_t channel, size_t bank_idx)
    {
        return fast_mod<IMPL::WB_QUEUE_SIZE>(channel * DRAM_TOT_BANKS_PER_CHANNEL + bank_idx) * bb_entries_per_bank_;
    }

    inline bool is_basic_set(size_t idx) const
    {
        return dram_column(idx) == dram_bank_idx(idx) && dram_row(idx) == 0;
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
