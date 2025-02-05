/* author: Suhas Vittal
 *  date:   31 January 2025
 * */

#ifndef CACHE_h
#define CACHE_h

#include "cache/entry.h"
#include "cache/enums.h"
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

template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
class Cache
{
public:
    using next_ptr =       std::unique_ptr<NEXT_TYPE>;
    using stat_type =      VecStat<uint32_t, NUM_THREADS>;
    using in_queue_type =  std::vector<Transaction>;
    using pending_type =   std::unordered_set<uint64_t>;

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

    struct cset_type : std::array<CacheEntry, NUM_WAYS>
    {
        using parent_type = std::array<CacheEntry, NUM_WAYS>;

        inline typename parent_type::iterator 
        find(uint64_t address)
        {
            return std::find_if(parent_type::begin(), parent_type::end(),
                        [address] (const auto& e) { return e.valid && e.address == address; });
        }
    };
    
    using psel_type = int16_t;

    using cset_array =      std::array<cset_type, NUM_SETS>;
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
public:
    Cache(std::string cache_name, next_ptr&);

    void warmup_access(uint64_t, bool write);
    void warmup_fill(uint64_t, bool dirty);

    virtual void tick(void);

    virtual bool can_accept(uint64_t, TransactionType);
    virtual bool can_accept_fill(void);

    virtual bool add_incoming(Transaction);
    virtual bool add_incoming_fill(Transaction);

    bool deadlock_find_inst(inst_ptr) const;
    /*
     * Useful inlines:
     * */
    virtual inline size_t set_index(uint64_t x) const
    {
        return fast_mod<NUM_SETS>(x);
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
    virtual bool probe(uint64_t, bool write=false);
    virtual bool mark(uint64_t, bool dirty);
    virtual void invalidate(uint64_t);
    /*
     * Cache fill implementations:
     * */
    virtual multi_fill_result_type fill(uint64_t, size_t num_mshr_refs, bool dirty);
    virtual multi_fill_result_type fill_with_eager_writeback(uint64_t, size_t num_refs, bool dirty); 
    virtual multi_fill_result_type fill_with_same_set_row_harvest(uint64_t, size_t num_refs, bool dirty);

    virtual way_iterator find_victim(cset_type&);
    /*
     * Insertion implementation:
     * */
    virtual void update_entry(CacheEntry&);
    virtual void init_entry(CacheEntry&, uint64_t address, size_t num_refs, bool dirty);
    /*
     * Replacement implementation:
     * */
    way_iterator lru(cset_type&);
    way_iterator rand(cset_type&);
    way_iterator rrip(cset_type&);
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

    inline cset_type& get_set(uint64_t x)
    {
        return csets_[set_index(x)];
    }

    inline const cset_type& get_const_set(uint64_t x) const
    {
        return csets_.at(set_index(x));
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

template <class ITER> inline size_t 
cset_get_lru_position_of_entry(const CacheEntry& e, ITER begin, ITER end)
{
    return std::count_if(begin, end,
                        [t=e.timestamp] (const auto& x) { return t > x.timestamp; });
}

template <class ITER> inline ITER
cset_get_way_in_lru_position(ITER begin, ITER end, size_t p)
{
    if (p == 0)
    {
        return std::min_element(begin, end,
                        [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
    }
    else
    {
        return std::find_if(begin, end,
                    [p, &begin, &end] (const auto& e) { return cset_get_lru_position_of_entry(e, begin, end) == p; });
    }
}

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
