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

#define WRP_ORDER   TransactionType::WRITE, TransactionType::READ, TransactionType::PREFETCH
#define RPW_ORDER   TransactionType::READ, TransactionType::PREFETCH, TransactionType::WRITE

inline void
dec_pending(IOBus::pending_type& p, uint64_t addr)
{
    if ((--p[addr]) == 0)
        p.erase(addr);
}

typename IOBus::opt_trans_type
IOBus::get_next_available_request()
{
    opt_trans_type out;

    if (write_queue_.size() == wq_size_)
        out = search_for_available_request_in_given_order<WRP_ORDER>();
    else
        out = search_for_available_request_in_given_order<RPW_ORDER>();

    if (out.has_value())
    {
        const Transaction& trans = out.value();
        if (trans_is_read(trans.type))
            dec_pending(pending_reads_, trans.address);
        else
            dec_pending(pending_writes_, trans.address);
    }
    return out;
}

#undef WRP_ORDER
#undef RPW_ORDER

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
IOBus::add_incoming(Transaction t)
{
    if (trans_is_read(t.type))
        ++s_reads_;
    else
        ++s_writes_;
    // Check for forwarding.
    if (pending_writes_.count(t.address))
    {
        if (trans_is_read(t.type))
            add_outgoing(t, 1);
        return true;
    }

    // Same thing for reads: merge if there is an existing read already.
    if (trans_is_read(t.type) && pending_reads_.count(t.address))
    {
        auto rd_it = std::find_if(read_queue_.begin(), read_queue_.end(),
                            [addr = t.address] (const Transaction& x)
                            {
                                return x.address == addr;
                            });
        rd_it->merge(t);
        return true;
    }

    // Add to requisite queue.
    if (trans_is_read(t.type))
    {
        size_t s = (t.type == TransactionType::PREFETCH) ? pq_size_ : rq_size_;
        in_queue_type& q = (t.type == TransactionType::PREFETCH) ? prefetch_queue_ : read_queue_;
        if (q.size() == s)
            return false;
        else
            q.push_back(t);
        ++pending_reads_[t.address];
    } 
    else
    {
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
    if (t.type == TransactionType::READ || t.type == TransactionType::TRANSLATION)
        outgoing_queue_.emplace(t, GL_CYCLE+latency);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
IOBus::deadlock_find_inst(const inst_ptr inst)
{
    std::cerr << "\tio status: RQ = " << read_queue_.size()
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
IOBus::deadlock_search_in_queue(std::string_view qname, const std::deque<Transaction>& q, const inst_ptr inst)
{
    auto q_it = std::find_if(q.cbegin(), q.cend(),
                            [inst] (const Transaction& t)
                            {
                                return t.contains_inst(inst);
                            });
    if (q_it != q.end())
    {
        size_t dist = std::distance(q.begin(), q_it);
        std::cerr << "\tfound in " << qname << ", entry #" << dist << "\n";
        return true;
    }
    else
        return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
