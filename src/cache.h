/* author: Suhas Vittal
 *  date:   31 January 2025
 * */

#ifndef CACHE_h
#define CACHE_h

#include "cache/dead_block/base.h"
#include "cache/entry.h"
#include "cache/enums.h"
#include "cache/indexing.h"
#include "transaction.h"
#include "util/numerics.h"
#include "util/out_queue.h"
#include "util/stats.h"

#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
 *      -- size_t WB_QUEUE_SIZE
 *      -- size_t FILL_QUEUE_SIZE
 *
 *      -- size_t NUM_READ_PORTS
 *      -- size_t NUM_WRITE_PORTS
 *      -- size_t NUM_FILL_PORTS
 *
 *      -- CacheWBMode WRITEBACK_MODE
 *
 *      -- bool WRITE_ALLOCATE
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, class NEXT_TYPE>
class Cache
{
public:
    using next_ptr =       std::unique_ptr<NEXT_TYPE>;
    using stat_type =      VecStat<uint32_t, NUM_THREADS>;
    using in_queue_type =  std::vector<Transaction>;
    using pending_type =   std::unordered_set<uint64_t>;
    using dbp_ptr =        std::unique_ptr<DeadBlockPredictorBase>;

    stat_type s_reads_{};
    stat_type s_writes_{};
    stat_type s_accesses_{};
    stat_type s_misses_{};
    stat_type s_fills_{};
    stat_type s_tot_miss_penalty_{};
    stat_type s_num_miss_penalty_{};
    stat_type s_invalidates_{};
    stat_type s_write_alloc_{};

    uint32_t s_evictions_ =0;
    uint32_t s_writebacks_ =0;
    uint32_t s_eager_writebacks_ =0;

    uint32_t s_bypasses_ =0;

    uint32_t s_dueling_pol1_installs_ =0;
    uint32_t s_dueling_pol2_installs_ =0;
    /*
     * Stats exclusive to same-set-row-harvest:
     * */
    using ssrh_lru_pos_array = std::array<uint32_t, 4>;
    ssrh_lru_pos_array s_ssrh_tot_lru_pos_{};
    ssrh_lru_pos_array s_ssrh_num_harvests_{};

    out_queue_type outgoing_queue_;

    const std::string cache_name_;
protected:
    enum class SetDuelingRole { FOLLOWER =0, LEADER_1 =1, LEADER_2 =-1 };
    
    using psel_type = int16_t;

    using mshr_type =       std::unordered_multimap<uint64_t, MSHREntry>;
    using wb_queue_type =   std::deque<Transaction>;
    using fill_queue_type = std::deque<Transaction>;

    constexpr static size_t    LEADER_SETS = 64;
    constexpr static size_t    PSEL_WIDTH = 11;
    constexpr static psel_type PSEL_DEFAULT = (1<<(PSEL_WIDTH-1))-1;
    constexpr static psel_type PSEL_MSB_MASK = 1 << (PSEL_WIDTH-1);
    /*
     * Core cache structures:
     * */
    cset_array csets_{};
    psel_type  psel_ =PSEL_DEFAULT;
    size_t     bimodal_counter_ =0;
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
    next_ptr& next_;
    /*
     * Pointer to dead block predictor:
     * */
    dbp_ptr dbp_;
public:
    Cache(std::string cache_name, next_ptr&);

    void warmup_access(const Transaction&);
    void warmup_fill(const Transaction&);

    virtual void tick(void);

    virtual bool can_accept(uint64_t, TransactionType);
    virtual bool can_accept_fill(void);

    virtual bool add_incoming(Transaction);
    virtual bool add_incoming_fill(Transaction);

    bool deadlock_find_inst(inst_ptr) const;
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
     * Cache fill implementations:
     * */
    virtual multi_fill_result_type fill(const Transaction&);
    virtual multi_fill_result_type fill_with_eager_writeback(const Transaction&); 
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
     * Set Dueling implementation:
     * */
    virtual SetDuelingRole get_set_role(size_t set_idx) const;
    virtual void update_psel(size_t set_idx);
    /*
     * IO functions for R/W/P queues (`do_next_access`) and fills (`do_next_fill`)
     * */
    virtual void do_next_fill(void);
    virtual bool do_next_access(bool do_read);
    virtual void add_mshr_entry(Transaction);

    inline in_queue_type& get_queue_ref(TransactionType t)
    {
        if (t == TransactionType::PREFETCH)
            return prefetch_queue_;
        else if (t == TransactionType::WRITE)
            return write_queue_;
        else
            return read_queue_;
    }

    inline size_t get_queue_size(TransactionType t) const
    {
        if (t == TransactionType::PREFETCH)
            return IMPL::PQ_SIZE;
        else if (t == TransactionType::WRITE)
            return IMPL::WQ_SIZE;
        else
            return IMPL::RQ_SIZE;
    }

    virtual inline bool allow_access(void)
    {
        return mshr_.size() < IMPL::NUM_MSHR && writeback_queue_.size() < IMPL::WB_QUEUE_SIZE;
    }

    virtual inline bool allow_fill(void)
    {
        return !fill_queue_.empty() && writeback_queue_.size() < IMPL::WB_QUEUE_SIZE;
    }

    virtual inline void enqueue_writeback(Transaction trans)
    {
        writeback_queue_.push_back(trans);
        pending_writebacks_.insert(trans.address);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

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
