/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#include "globals.h"

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

bool
IOBus::add_incoming(Transaction t)
{
    // Check for forwarding.
    if (pending_writes_.count(t.address)) {
        if (trans_is_read(t.type))
            outgoing_queue_.emplace(t, GL_CYCLE+1);
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
