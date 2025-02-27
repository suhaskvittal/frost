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

#define MCP_ENABLE_LOGGER

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::MinimalistPartitionManager()
#if defined(MCP_ENABLE_LOGGER)
    :mcp_logger_("mcp.log")
#endif
{}

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

#if defined(MCP_ENABLE_LOGGER)
    mcp_logger_ << "\n===============================================\n"
                << "MCP @ CYCLE = " << GL_CYCLE << "\n"
                << std::setw(12) << std::left << "MONITOR" << std::setw(8) << "MISSES";

    for (size_t i = 0; i < IMPL::NUM_WAYS; i++)
        mcp_logger_ << std::setw(8) << ("HIT_" + std::to_string(i));

    mcp_logger_ << "\n" << std::setw(12) << "UMON" << std::setw(8) << umon_.total_misses;

    for (size_t i = 0; i < IMPL::NUM_WAYS; i++)
        mcp_logger_ << std::setw(8) << umon_.hit_counters[i];

    mcp_logger_ << "\n" << std::setw(12) << "WMON" << std::setw(8) << wmon_.total_misses;

    for (size_t i = 0; i < IMPL::NUM_WAYS; i++)
        mcp_logger_ << std::setw(8) << wmon_.hit_counters[i];
#endif

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

#if defined(UCP_ENABLE_LOGGER)
        mcp_logger_ << "\n\tALLOC W = " << i << "\tUTILITY = " << u;
#endif
    }

    std::fill(begin, end, best_way);
    victim_part_ = IMPL::NUM_WAYS - best_way;

#if defined(MCP_ENABLE_LOGGER)
    mcp_logger_ << "\nallocated " << victim_part_ << " ways to virtual buffer\n";
#endif

    // Update counters:
    for (auto u : {std::ref(umon_), std::ref(wmon_)})
    {
        auto& mon = u.get();
        for (auto& c : mon.hit_counters)
            c >>= 1;
        mon.total_misses >>= 1;
    }

#if defined(UCP_ENABLE_LOGGER)
    mcp_logger_.flush();
#endif
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
    auto r = umon_.atd.mark_dirty(trans, [] (const cset_type&, cset_type::const_iterator) {});
    if (r == ATDLookupResult::MISS)
    {
        // Try the write monitor -- if we still fail, do a fill
        r = wmon_.atd.mark_dirty(trans, [] (const cset_type&, cset_type::const_iterator) {});
        if (r == ATDLookupResult::MISS)
            umon_fill(trans);
    }
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

                    // Unlike UMON, we care about the LRU positions (how deep into the LRU stack do we need
                    // to go to get row buffer hits?)
                    std::vector<size_t> lru_positions;
                    
                    std::transform(s.begin(), s.end(), std::back_inserter(lru_positions),
                            [&s] (const auto& e)
                            {
                                return std::count_if(s.begin(), s.end(),
                                            [t=e.timestamp] (const auto& x) { return t > x.timestamp; });
                            });
                    
                    for (size_t i = 0; i < IMPL::NUM_WAYS; i++)
                    {
                        auto& e = s[i];
                        if (!e.valid)
                            continue;

                        if (e.address == v.address)
                        {
                            ++this->wmon_.hit_counters[0];
                            continue;
                        }

                        // Check if `e` has the same row as `s`:
                        bool is_row_hit = (bank_idx == dram_bank_idx(e.address)) && (row == dram_row(e.address));
                        if (is_row_hit)
                        {
                            ++this->wmon_.hit_counters[lru_positions[i]];

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
