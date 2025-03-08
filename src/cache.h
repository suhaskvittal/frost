/* author: Suhas Vittal
 *  date:   31 January 2025
 * */

#ifndef CACHE_h
#define CACHE_h

#include "constants.h"

#include "cache/dead_block/base.h"
#include "cache/ext/atd.h"
#include "cache/partitioning/base.h"
#include "cache/entry.h"
#include "cache/enums.h"
#include "cache/ext/set_dueling.h"
#include "cache/indexing.h"
#include "transaction.h"
#include "util/numerics.h"
#include "util/out_queue.h"
#include "util/stats.h"

#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fstream>
#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 *  `IMPL` Definition:
 *      -- size_t NUM_SETS
 *      -- size_t NUM_WAYS
 *      -- CacheReplPolicy REPL
 *
 *      -- size_t RQ_SIZE
 *      -- size_t WQ_SIZE
 *      -- size_t PQ_SIZE
 *
 *      -- uint64_t CACHE_LATENCY
 *      -- size_t NUM_MSHR
 *      -- size_t FILL_QUEUE_SIZE
 *
 *      -- size_t NUM_READ_PORTS
 *      -- size_t NUM_WRITE_PORTS
 *      -- size_t NUM_FILL_PORTS
 *
 *      -- CacheWritebackPolicy WRITEBACK_POLICY
 *
 *      -- bool WRITE_ALLOCATE
 *
 *      -- type DEAD_BLOCK_PREDICTOR_TYPE
 *      -- type PARTITION_MANAGER_TYPE
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, class NEXT_TYPE>
class Cache
{
public:
    using stat_type = VecStat<uint32_t, NUM_THREADS>;
    using stat_type_u64 = VecStat<uint64_t, NUM_THREADS>;
    
    stat_type s_reads_{};
    stat_type s_writes_{};
    stat_type s_read_forwards_{};
    stat_type s_write_forwards_{};
    stat_type s_rewrites_{};
    stat_type s_accesses_{};
    stat_type s_misses_{};
    stat_type s_fills_{};
    stat_type s_invalidates_{};
    stat_type s_write_alloc_{};

    stat_type_u64 s_tot_miss_penalty_{};
    stat_type_u64 s_num_miss_penalty_{};

    uint32_t s_evictions_ =0;
    uint32_t s_writebacks_ =0;
    uint32_t s_eager_writebacks_ =0;
    uint32_t s_demand_writebacks_ =0;
    uint32_t s_bypasses_ =0;
    uint32_t s_dead_block_evictions_ =0;
    /*
     * Set Dueling Stats:
     * */
    uint32_t s_dueling_pol1_installs_ =0;
    uint32_t s_dueling_pol2_installs_ =0;
    /*
     * Cache Partitioning Stats:
     * */
    stat_type s_lifetime_way_alloc_{};
    uint32_t  s_total_way_allocs =0;

    out_queue_type outgoing_queue_;

    const std::string cache_name_;
protected:
    using in_queue_type =  std::deque<Transaction>;
    using pending_type =   std::unordered_multiset<uint64_t>;
    using mshr_type =       std::unordered_multimap<uint64_t, MSHREntry>;
    using wb_queue_type =   std::deque<Transaction>;
    using fill_queue_type = std::deque<Transaction>;
    /*
     * Core cache structures:
     * */
    cset_array csets_{};
    /*
     * I/O structures
     * */
    in_queue_type read_queue_;
    in_queue_type write_queue_;
    in_queue_type prefetch_queue_;

    pending_type  pending_reads_;
    pending_type  pending_writes_;
    pending_type  pending_misses_;
    pending_type  pending_writebacks_;
    /*
     * MSHR and writeback queue structures:
     * */ 
    mshr_type       mshr_;
    wb_queue_type   writeback_queue_;
    fill_queue_type fill_queue_;
    size_t num_mshr_asleep_ =0;
    /*
     * Pointer to next structure in the memory hierarchy.
     * */
    using next_ptr = std::unique_ptr<NEXT_TYPE>;

    next_ptr& next_;
    /*
     * Pointer to dead block predictor:
     * */
    using dbp_ptr = std::unique_ptr<DeadBlockPredictorBase>;

    dbp_ptr dead_block_pred_;
    /*
     * Below this are non-standard cache implementation structures (i.e., set dueling, cache partitioning).
     * These are non-standard in the sense that they are not offered in standard simulators.
     *
     * To isolate each one and prevent name collisions, we implement them in their own structs where appropriate.
     *
     * Some structures like `ucp_` will be implemented as pointers as they are expensive to allocate (i.e., UCP
     * requires ATDs per core). If they are not used, they will be left as null.
     * */
    using cpart_ptr = std::unique_ptr<PartitionManagerBase>;

    SetDuelingMonitor set_dueling_arbiter_{};

    cache_partition_array partition_;
    cpart_ptr             partition_manager_;

    /*
     * Optional "utility tracker" -- only used if `CACHE_ENABLE_UTILITY_TRACKER` is defined. Will write
     * to <cache_name>.u.log
     * */
    struct utility_tracker_type
    {
        /*
         * Each vector will have `IMPL::NUM_WAYS+1` entries. The last entry is, for example, the number of misses
         * */
        std::vector<size_t> hits;
        std::vector<size_t> writebacks;
        std::vector<size_t> dead_blocks;

        AuxTagDirectory<IMPL, 256> atd;

        utility_tracker_type(void)
            :hits(IMPL::NUM_WAYS+1, 0),
            writebacks(IMPL::NUM_WAYS+1, 0),
            dead_blocks(IMPL::NUM_WAYS+1, 0)
        {}
    };

    using utility_tracker_ptr = std::unique_ptr<utility_tracker_type>;

    utility_tracker_ptr utr_;
    std::ofstream       utr_out_;
public:
    Cache(std::string cache_name, next_ptr&);

    void warmup_access(const Transaction&);
    void warmup_fill(const Transaction&);

    virtual void tick(void);
    virtual bool add_incoming(Transaction);
    virtual bool add_incoming_fill(Transaction);

    bool deadlock_find_inst(inst_ptr) const;
    /*
     * Useful inlines:
     * */
    virtual inline bool can_accept(const Transaction& trans) const
    {
        return get_const_queue_ref(trans.type).size() < get_queue_size(trans.type);
    }

    virtual inline bool can_accept_fill(void) const
    {
        return fill_queue_.size() < IMPL::FILL_QUEUE_SIZE;
    }

    inline size_t write_occu(void) const
    {
        return std::transform_reduce(csets_.begin(), csets_.end(), 0,
                            std::plus<size_t>{},
                            [] (const auto& s)
                            {
                                return std::count_if(s.begin(), s.end(),
                                                [] (const auto& e) { return e.valid && e.dirty; });
                            });
    }
protected:
    struct fill_result_type
    {
        CacheEntry entry{};
        size_t lru_pos =0;

        fill_result_type(void) =default;
        fill_result_type(CacheEntry e, size_t p)
            :entry(std::move(e)),
            lru_pos(p)
        {}
    };

    using multi_fill_result_type = std::vector<fill_result_type>;
    using way_iterator = typename cset_type::iterator;
    /*
     * Cache access implementation:
     * */
    virtual bool probe(const Transaction&);
    virtual bool mark_dirty(const Transaction&);
    virtual void invalidate(uint64_t);
    /*
     * Child functions for handling hits in `probe` and `mark_dirty`
     * */
    virtual void child_handle_probe_hit(cset_type&, cset_type::iterator, const Transaction&) {}
    virtual void child_handle_mark_dirty_hit(cset_type&, cset_type::iterator, const Transaction&) {}
    /*
     * Cache fill implementations:
     * */
    virtual multi_fill_result_type fill(const Transaction&);
    virtual multi_fill_result_type fill_with_eager_writeback(const Transaction&); 
    virtual multi_fill_result_type fill_with_ssrh(const Transaction&);
    /*
     * When searching for a victim, we also provide the calling Transaction in case the replacement
     * policy would like to bypass, in which case the `way_iterator` should be the end of the `cset_type`.
     * */
    virtual way_iterator find_victim(size_t set_index, cset_type&, const Transaction&);
    /*
     * Insertion implementation:
     * */
    virtual void update_entry(CacheEntry&);
    virtual void init_entry(CacheEntry&, const Transaction&);
    /*
     * Replacement implementation:
     * */
    way_iterator repl_lru(cset_type&, const Transaction&);
    way_iterator repl_rand(cset_type&, const Transaction&);
    way_iterator repl_rrip(cset_type&, const Transaction&);

    way_iterator repl_lru_dead_block(cset_type&, const Transaction&);
    /*
     * IO functions for R/W/P queues (`do_next_access`) and fills (`do_next_fill`)
     * */
    virtual void do_next_fill(void);
    virtual bool do_next_access(bool do_read);
    virtual void add_mshr_entry(Transaction);

    inline in_queue_type& get_queue_ref(Transaction::Type t)
    {
        return const_cast<in_queue_type&>(get_const_queue_ref(t));
    }

    inline const in_queue_type& get_const_queue_ref(Transaction::Type t) const
    {
        if (t == Transaction::Type::PREFETCH)
            return prefetch_queue_;
        else if (t == Transaction::Type::WRITE)
            return write_queue_;
        else
            return read_queue_;
    }

    inline size_t get_queue_size(Transaction::Type t) const
    {
        if (t == Transaction::Type::PREFETCH)
            return IMPL::PQ_SIZE;
        else if (t == Transaction::Type::WRITE)
            return IMPL::WQ_SIZE;
        else
            return IMPL::RQ_SIZE;
    }

    inline bool mshr_has_space(void) const
    {
        return mshr_.size() + writeback_queue_.size() < IMPL::NUM_MSHR;
    }

    virtual inline bool allow_access(void) const
    {
        return mshr_has_space();
    }

    virtual inline bool allow_fill(void) const
    {
        return !fill_queue_.empty();
    }

    virtual inline void enqueue_writeback(Transaction trans)
    {
        writeback_queue_.push_back(trans);
        pending_writebacks_.insert(trans.address);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class ITER>
void dump_utility_info(std::ostream& out, ITER begin, ITER end, std::string_view name);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class CACHE_TYPE, class FUNC>
void drain_cache_outgoing_queue(std::unique_ptr<CACHE_TYPE>&, const FUNC&);

void drain_llc_outgoing_queue(void);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cache.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_h
