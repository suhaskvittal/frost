/*
 *  author: Suhas Vittal
 *  date:   24 February 2025
 * */

#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL>
#define __TEMPLATE_CLASS__ MinimalistPartitionManager<IMPL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_partition(part_iterator begin, part_iterator end)
{
    if constexpr (NUM_THREADS == 1)
        return;

    // The algorithm is just like UCP for two threads:
    //  Key difference: we can give all ways to workloads
    //      -- this is a subtle difference in the for loop: `i <= IMPL::NUM_WAYS` instead of `i < IMPL::NUM_WAYS`

    size_t best_way = 0;
    size_t best_way_util = std::numeric_limits<size_t>::max();
    for (size_t i = 1; i <= IMPL::NUM_WAYS; i++)
    {
        size_t u = umon_.utility(i) + wmon_.utility(IMPL::NUM_WAYS-i);
        if (u < best_way_util)
        {
            best_way = i;
            best_way_util = u;
        }
    }

    std::fill(begin, end, best_way);
    victim_part_ = IMPL::NUM_WAYS - best_way;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_on_probe(const Transaction& trans)
{
    // First check `wmon_`. If we get a hit, then invalidate the entry.
    wmon_.atd.probe(trans,
                    [] (cset_type& s, cset_type::iterator it)
                    {
                        if (it != s.end())
                            it->valid = false;
                    });

    // Lookup in `umon_`
    auto r = umon_.atd.probe(trans,
                    [this] (const cset_type& s, cset_type::const_iterator it)
                    {
                        if (it == s.end())
                        {
                            ++this->umon_.total_misses;
                        }
                        else
                        {
                            // Compute MRU location:
                            size_t p = std::count_if(s.begin(), s.end(),
                                            [t=it->timestamp] (const auto& e) { return e.timestamp > t; });
                            ++this->umon_.hit_counters[p];
                        }
                    });
    if (r == ATDLookupResult::MISS)
        umon_fill(trans);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_on_mark_dirty(const Transaction& trans)
{
    // Check if the entry is in `umon_`:
    auto r = umon_.atd.mark_dirty(trans);
    if (r == ATDLookupResult::MISS)
        umon_fill(trans);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::umon_fill(const Transaction& trans)
{
    auto victim = umon_.atd.fill(trans, [] (const cset_type&, const CacheEntry&) {});

    // If we have a writeback, then move the line to `wmon_`.
    if (victim.valid && victim.dirty)
    {
        Transaction wb_trans{trans.coreid, trans.ip, victim.address, nullptr, Transaction::Type::WRITE};
        
        // Fill `wmon_` and update writeback counts:
        wmon_.atd.fill(wb_trans, 
                [this] (cset_type& s, const CacheEntry& v)
                {
                    if (!v.valid || !v.dirty)
                        return;

                    size_t bank_idx = dram_bank_idx(v.address),
                           row = dram_row(v.address);
                    
                    for (auto& e : s)
                    {
                        // Check if `e` has the same row as `s`:
                        bool is_row_hit = (bank_idx == dram_bank_idx(e.address)) && (row == dram_row(e.address));
                        if (is_row_hit)
                        {
                            size_t p = std::count_if(s.begin(), s.end(),
                                                [t=e.timestamp] (const auto& x) { return x.timestamp > t; });
                            ++this->wmon_.hit_counters[p];

                            // Invalidate the entry -- simulate a writeback
                            e.valid = false;
                        }
                    }

                    // Note that `total_misses` is still updated as the assumption is that this new entry is a miss.
                    ++this->wmon_.total_misses;
                });
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
