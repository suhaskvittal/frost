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

    using typename __TEMPLATE_PARENT__::SetDuelingRole;

    constexpr static size_t MAX_WRITE_COUNTER = DRAM_WQ_SIZE / DRAM_TOT_BANKS_PER_CHANNEL;
    /*
     * These structures are used to buffer writes and determine when to use the modified replacement policy
     * (i.e., see `lru_mod` and `rrip_mod` below)
     * */
    local_counter_array  balance_counters_{};
    global_counter_array total_writebacks_{};
    bool_array           in_write_mode_{};

    using __TEMPLATE_PARENT__::next_;
    using __TEMPLATE_PARENT__::dbp_;
    using __TEMPLATE_PARENT__::mshr_;
    using __TEMPLATE_PARENT__::writeback_queue_;
    using __TEMPLATE_PARENT__::pending_writebacks_;
public:
    using write_counts_array = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;

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
    way_iterator find_victim(size_t idx, cset_type&, const Transaction&) override;

    way_iterator repl_lru_mod(cset_type&, const Transaction&);
    way_iterator repl_rrip_mod(cset_type&, const Transaction&);
    bool         bypass_repl(const Transaction&, bool prio_write);
    way_iterator check_dead_block_prediction(cset_type&, const Transaction&, bool prio_write);

    void enqueue_writeback(Transaction) override;

    inline SetDuelingRole get_set_role(size_t idx) const override
    {
        size_t bank_idx = dram_bank_idx(idx) + dram_channel(idx)*DRAM_TOT_BANKS_PER_CHANNEL;
        size_t col = dram_column(idx);
        size_t inv_col = dram_column(idx) ^ (DRAM_COLUMNS-1);

        if (dram_row(idx) == 0)
        {
            if (bank_idx == col)
                return SetDuelingRole::LEADER_1;
            else if (bank_idx == inv_col)
                return SetDuelingRole::LEADER_2;
        }
        return SetDuelingRole::FOLLOWER;
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
