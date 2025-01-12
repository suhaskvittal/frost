/* 
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#ifndef IO_BUS_h
#define IO_BUS_h

#include <algorithm>
#include <deque>
#include <memory>
#include <unordered_map>
#include <optional>
#include <queue>
#include <string_view>
#include <vector>

#include "transaction.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class IOBus
{
public:
    using opt_trans_type = std::optional<Transaction>;
    /*
     * `out_trans_type`: (1) the `Transaction`, and (2) the cycle it is ready.
     * */
    using out_trans_type = std::tuple<Transaction, uint64_t>;
    struct out_queue_cmp
    {
        inline bool operator()(const out_trans_type& x, const out_trans_type& y)
        {
            return std::get<1>(x) > std::get<1>(y);
        }
    };

    using in_queue_type = std::deque<Transaction>;
    using pending_type = std::unordered_map<uint64_t, size_t>;
    using out_queue_type = std::priority_queue<out_trans_type, 
                                            std::vector<out_trans_type>,
                                            out_queue_cmp>;
    /*
     * This is a queue for outgoing transactions. It has
     * no size and only holds reads.
     *
     * Makes sense to make this public as other classes
     * will manipulate the outputs.
     * */
    out_queue_type outgoing_queue_;

    uint64_t s_blocking_writes_ =0;
    /*
     * Queue sizes for each of the input queues.
     * */
    const size_t rq_size_;
    const size_t wq_size_;
    const size_t pq_size_;
private:
    /*
     * These are all inputs:
     * */
    in_queue_type read_queue_;
    in_queue_type write_queue_;
    in_queue_type prefetch_queue_;

    pending_type pending_reads_;
    pending_type pending_writes_;

    size_t writes_to_drain_ =0;
public:
    IOBus(size_t rq_size, size_t wq_size, size_t pq_size);
    /*
     * Returns the next available transaction (if one exists). If a
     * predicate is provided, the selected transaction will only be
     * returned if the predicate returns true.
     * */
    template <class PRED>
    opt_trans_type get_next_incoming(PRED);

    inline opt_trans_type get_next_incoming()
    {
        return get_next_incoming([] (const Transaction&) { return true; });
    }
    /*
     * Pushes the given transaction onto the appropriate queue.
     * Returns `false` if there is no space.
     * */
    bool add_incoming(Transaction);
    void add_outgoing(Transaction, uint64_t latency);
    /*
     * Searches for references to the instruction in the queues. 
     * Returns true if found and writes to stderr.
     * */
    bool deadlock_find_inst(const inst_ptr);

    inline bool can_accept(TransactionType t)
    {
        if (t == TransactionType::READ || t == TransactionType::TRANSLATION)
            return read_queue_.size() < rq_size_;
        else if (t == TransactionType::WRITE)
            return write_queue_.size() < wq_size_;
        else
            return prefetch_queue_.size() < pq_size_;
    }
private:
    bool deadlock_search_in_queue(std::string_view qname, const in_queue_type&, const inst_ptr);

    inline void dec_pending(pending_type& p, uint64_t addr)
    {
        if ((--p[addr]) == 0)
            p.erase(addr);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class PRED> typename IOBus::opt_trans_type
IOBus::get_next_incoming(PRED pred)
{
    opt_trans_type out;
    // Need to drain writes if the queue is full, or we can also
    // do it if there is nothing left to do.
    bool write_drain_cond = write_queue_.size() == wq_size_
                            || (read_queue_.empty() && prefetch_queue_.empty() && !write_queue_.empty());
    if (writes_to_drain_ == 0 && write_drain_cond)
        writes_to_drain_ = write_queue_.size();

    bool access_done = false;
    if (writes_to_drain_ > 0)
    {
        auto w_it = std::find_if(write_queue_.begin(), write_queue_.end(),
                            [this, pred] (const Transaction& t)
                            {
                                return this->pending_reads_.count(t.address) == 0 && pred(t);
                            });
        if (w_it != write_queue_.end())
        {
            access_done = true;
            out = *w_it;
            
            if (!read_queue_.empty() || !prefetch_queue_.empty())
                ++s_blocking_writes_;

            dec_pending(pending_writes_, w_it->address);
            --writes_to_drain_;
            write_queue_.erase(w_it);
        } 
        else if (!write_queue_.empty())
            writes_to_drain_ = 0;  // Cannot proceed with writes -- might as well switch back to reads.
    }

    if (!access_done)
    {
        in_queue_type& q = read_queue_.empty() ? prefetch_queue_ : read_queue_;
        auto r_it = std::find_if(q.begin(), q.end(), pred);
        if (r_it != q.end())
        {
            dec_pending(pending_reads_, r_it->address);
            out = *r_it;
            q.erase(r_it);
        }
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // IO_BUS_h
