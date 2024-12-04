/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#ifndef IO_BUS_h
#define IO_BUS_h

#include <deque>
#include <unordered_map>
#include <optional>
#include <queue>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class IOBus
{
public:
    /*
     * `out_trans_t`: (1) the `Transaction`, and (2) the cycle it is ready.
     * */
    using out_trans_t = std::tuple<Transaction, uint64_t>;
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
    struct out_queue_cmp
    {
        inline bool operator()(const out_trans_t& x, const out_trans_t& y)
        {
            return std::get<1>(x) > std::get<1>(y);
        }
    }

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
    using trans_t = std::optional<Transaction>;
    using trans_pred_t = std;:function<bool(const Transaction&)>;

    IOBus(size_t rq_size, size_t wq_size, size_t pq_size);
    /*
     * Returns the next available transaction (if one exists). If a
     * predicate is provided, the selected transaction will only be
     * returned if the predicate returns true.
     * */
    trans_t get_next_incoming(void);
    trans_t get_next_incoming(trans_pred_t);
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

using io_ptr = std::unique_ptr<IOBus>; 

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // IO_BUS_h
