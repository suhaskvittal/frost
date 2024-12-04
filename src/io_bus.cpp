/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#include "io_bus.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

IOBus::IOBus(size_t r, size_t w, size_t p)
    :rq_size_(r),
    wq_size_(w),
    pq_size_(p)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

IOBus::trans_t
IOBus::get_next_incoming()
{
    return get_next_incoming([] (const Transaction&) { return true; });
}

IOBus::trans_t
IOBus::get_next_incoming(trans_pred_t pred)
{
    trans_t out;
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
            dec_pending(pending_write_, w_it->address);
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

bool
IOBus::add_incoming(Transaction t)
{
    // Check for forwarding.
    if (pending_writes_.count(t.address)) {
        if (trans_is_read(t.type)) {
            outgoing_queue_.emplace(t, GL_CYCLE+1);
        }
        return true;
    }
    // Add to requisite queue.
    if (trans_is_read(t.type)) {
        size_t s = (t.type == TransactionType::PREFETCH) ? pq_size_ : rq_size_;
        in_queue_t& q = (t.type == TransactionType::PREFETCH) ? prefetch_queue_ : read_queue_;
        if (q.size() == s)
            return false;
        else
            q.push_back(t);
        ++pending_reads_[t.address];
    } else {
        if (write_queue_.size() == wq_size_)
            return false;
        else
            write_queue_.push_back(t);
        ++pending_writes_[t.address];
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
