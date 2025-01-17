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
    using out_queue_type = std::unordered_multimap<uint64_t, Transaction>;
    /*
     * This is a queue for outgoing transactions. It has
     * no size and only holds reads.
     *
     * Makes sense to make this public as other classes
     * will manipulate the outputs.
     * */
    out_queue_type outgoing_queue_;

    uint64_t s_reads_ =0;
    uint64_t s_writes_ =0;
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
public:
    IOBus(size_t rq_size, size_t wq_size, size_t pq_size);
    /*
     * Returns the next available transaction (if one exists).
     * */
    opt_trans_type get_next_available_request(void);
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

    inline bool can_accept(uint64_t address, TransactionType t)
    {
        if (t == TransactionType::PREFETCH)
            return prefetch_queue_.size() < pq_size_;
        else if (trans_is_write(t))
            return write_queue_.size() < wq_size_;
        else
            return read_queue_.size() < rq_size_;
    }
private:
    template <TransactionType T>
    opt_trans_type search_for_available_request(void);

    template <TransactionType T1, TransactionType T2, TransactionType T3>
    opt_trans_type search_for_available_request_in_given_order(void);

    bool deadlock_search_in_queue(std::string_view qname, const in_queue_type&, const inst_ptr);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <TransactionType T> typename IOBus::opt_trans_type
IOBus::search_for_available_request()
{
    opt_trans_type out;

    if constexpr (T == TransactionType::WRITE)
    {
        if (write_queue_.empty())
            return out;
        auto w_it = std::find_if(write_queue_.begin(), write_queue_.end(),
                            [this] (const Transaction& t)
                            {
                                return this->pending_reads_.count(t.address) == 0;
                            });
        if (w_it != write_queue_.end())
        {
            out.emplace(std::move(*w_it));
            write_queue_.erase(w_it);
        }
    }
    else
    {
        auto& q = (T == TransactionType::READ) ? read_queue_ : prefetch_queue_;
        if (q.empty())
            return out;
        out.emplace(std::move(q.front()));
        q.pop_front();
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <TransactionType T1, TransactionType T2, TransactionType T3>
inline typename IOBus::opt_trans_type
IOBus::search_for_available_request_in_given_order()
{
    opt_trans_type out = search_for_available_request<T1>();
    if (!out.has_value())
        out = search_for_available_request<T2>();
    if (!out.has_value())
        out = search_for_available_request<T3>();
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // IO_BUS_h
