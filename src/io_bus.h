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
#include <vector>

#include "transaction.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class IOBus
{
public:
    using opt_trans_t = std::optional<Transaction>;
    /*
     * `out_trans_t`: (1) the `Transaction`, and (2) the cycle it is ready.
     * */
    using out_trans_t = std::tuple<Transaction, uint64_t>;
    struct out_queue_cmp
    {
        inline bool operator()(const out_trans_t& x, const out_trans_t& y)
        {
            return std::get<1>(x) > std::get<1>(y);
        }
    };
    using out_queue_t = std::priority_queue<out_trans_t, 
                                            std::vector<out_trans_t>,
                                            out_queue_cmp>;
    /*
     * This is a queue for outgoing transactions. It has
     * no size and only holds reads.
     *
     * Makes sense to make this public as other classes
     * will manipulate the outputs.
     * */
    out_queue_t outgoing_queue_;
    /*
     * Queue sizes for each of the input queues.
     * */
    const size_t rq_size_;
    const size_t wq_size_;
    const size_t pq_size_;
private:
    using in_queue_t = std::deque<Transaction>;
    using pending_t = std::unordered_map<uint64_t, size_t>;
    /*
     * These are all inputs:
     * */
    in_queue_t read_queue_;
    in_queue_t write_queue_;
    in_queue_t prefetch_queue_;

    pending_t pending_reads_;
    pending_t pending_writes_;

    size_t writes_to_drain_ =0;
public:
    IOBus(size_t rq_size, size_t wq_size, size_t pq_size);
    /*
     * Returns the next available transaction (if one exists). If a
     * predicate is provided, the selected transaction will only be
     * returned if the predicate returns true.
     * */
    template <class PRED>
    opt_trans_t get_next_incoming(PRED);

    inline opt_trans_t get_next_incoming()
    {
        return get_next_incoming([] (const Transaction&) { return true; });
    }
    /*
     * Pushes the given transaction onto the appropriate queue.
     * Returns `false` if there is no space.
     * */
    bool add_incoming(Transaction);
private:
    inline void dec_pending(pending_t& p, uint64_t addr)
    {
        if ((--p[addr]) == 0)
            p.erase(addr);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class PRED> IOBus::opt_trans_t
IOBus::get_next_incoming(PRED pred)
{
    opt_trans_t out;
    // Need to drain writes if the queue is full, or we can also
    // do it if there is nothing left to do.
    if (write_queue_.size() == wq_size_ 
        || (read_queue_.empty() && prefetch_queue_.empty() && !write_queue_.empty()))
    {
        writes_to_drain_ = write_queue_.size();
    }

    if (writes_to_drain_ > 0) {
        auto w_it = std::find_if(write_queue_.begin(), write_queue_.end(),
                            [this] (Transaction& t)
                            {
                                return this->pending_reads_.count(t.address) == 0;
                            });
        if (w_it != write_queue_.end() && pred(*w_it)) {
            out = *w_it;

            --writes_to_drain_;
            write_queue_.erase(w_it);
            dec_pending(pending_writes_, w_it->address);
        }
    } else {
        in_queue_t& q = read_queue_.empty() ? prefetch_queue_ : read_queue_;
        if (!q.empty() && pred(q.front())) {
            out = q.front();

            dec_pending(pending_reads_, out.value().address);
            q.pop_front();
        }
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // IO_BUS_h
