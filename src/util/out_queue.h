/*
 *  author: Suhas Vittal
 *  date:   1 February 2025
 * */

#ifndef UTIL_OUTGOING_QUEUE_h
#define UTIL_OUTGOING_QUEUE_h

#include "transaction.h"

#include <queue>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

using out_queue_entry_type = std::pair<Transaction, uint64_t>;

struct out_queue_cmp_type
{
    inline bool operator()(const out_queue_entry_type& x, const out_queue_entry_type& y) const
    {
        return x.second > y.second;
    }
};

using out_queue_type = std::priority_queue<out_queue_entry_type,
                                                std::vector<out_queue_entry_type>,
                                                out_queue_cmp_type>;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // UTIL_OUTGOING_QUEUE_h
