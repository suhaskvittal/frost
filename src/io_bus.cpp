/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#include "io_bus.h"

#include <algorithm>
#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern uint64_t GL_CYCLE;

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
            add_outgoing(t, 1);
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

void
IOBus::add_outgoing(Transaction t, uint64_t latency)
{
    if (trans_is_read(t.type)) {
        if (t.type == TransactionType::READ || t.type == TransactionType::TRANSLATION)
            outgoing_queue_.emplace(t, GL_CYCLE+latency);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
IOBus::deadlock_find_inst(const iptr_t& inst)
{
    std::cerr << "\tio status: writes_to_drain = " << writes_to_drain_ 
                << ", RQ = " << read_queue_.size()
                << ", WQ = " << write_queue_.size()
                << ", PQ = " << prefetch_queue_.size()
                << "\n\tpending_reads:\n";
    for (auto& [addr, cnt] : pending_reads_)
        std::cerr << "\t" << addr << " : " << cnt << "\n";
    std::cerr << "\tpending writes:\n";
    for (auto& [addr, cnt] : pending_writes_)
        std::cerr << "\t" << addr << " : " << cnt << "\n";
    std::cerr << "\n";

    bool found = false;
    found |= deadlock_search_in_queue("read_queue", read_queue_, inst);
    found |= deadlock_search_in_queue("write_queue", write_queue_, inst);
    return found;
}

bool
IOBus::deadlock_search_in_queue(std::string_view qname, const std::deque<Transaction>& q, const iptr_t& inst)
{
    auto q_it = std::find_if(q.cbegin(), q.cend(),
                            [inst] (const Transaction& t)
                            {
                                return t.inst == inst;
                            });
    if (q_it != q.end()) {
        size_t dist = std::distance(q.begin(), q_it);
        std::cerr << "\tfound in " << qname << ", entry #" << dist << "\n";
        return true;
    } else {
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
