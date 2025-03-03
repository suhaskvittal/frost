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
    :wb_buckets_(IMPL::NUM_WAYS+1, 0),
#if defined(MCP_ENABLE_LOGGER)
    mcp_logger_("mcp.log")
#endif
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_partition(part_iterator begin, part_iterator end)
{
    // The algorithm is just like UCP for two threads:
    //  Key difference: we can give all ways to workloads
    //      -- this is a subtle difference in the for loop: `i <= IMPL::NUM_WAYS` instead of `i < IMPL::NUM_WAYS`

#if defined(MCP_ENABLE_LOGGER)
    mcp_logger_ << "\n===============================================\n"
                << "MCP @ CYCLE = " << GL_CYCLE << "\n"
                << std::setw(12) << std::left << "MONITOR" << std::setw(8) << "MISSES";

    for (size_t i = 0; i < IMPL::NUM_WAYS; i++)
        mcp_logger_ << std::setw(8) << ("HIT_" + std::to_string(i));

    // Share `umon_` counters:
    mcp_logger_ << "\n" << std::setw(12) << "UMON" << std::setw(8) << umon_.total_misses;
    for (size_t i = 0; i < IMPL::NUM_WAYS; i++)
        mcp_logger_ << std::setw(8) << umon_.hit_counters[i];

    // Share writeback buckets:
    mcp_logger_ << "\n" << std::setw(12) << "WB" << std::setw(8) << wb_buckets_[0];
    for (size_t i = 1; i <= IMPL::NUM_WAYS; i++)
        mcp_logger_ << std::setw(8) << wb_buckets_[i];
#endif

    size_t best_way = 0;
    ssize_t best_way_util = std::numeric_limits<ssize_t>::max();
    for (size_t i = 1; i <= IMPL::NUM_WAYS; i++)
    {
        ssize_t lhs_u = umon_.utility(i),
                rhs_u = wb_utility(IMPL::NUM_WAYS-i);

        ssize_t u = lhs_u + rhs_u;
        if (u < best_way_util)
        {
            best_way = i;
            best_way_util = u;
        }

#if defined(UCP_ENABLE_LOGGER)
        mcp_logger_ << "\n\tALLOC W = " << i << "\tUTILITY = " << lhs_u << " + " << rhs_u << " = " << u;
#endif
    }

    std::fill(begin, end, best_way);
    victim_part_ = IMPL::NUM_WAYS - best_way;

#if defined(MCP_ENABLE_LOGGER)
    mcp_logger_ << "\nallocated " << victim_part_ << " ways to virtual buffer\n";
#endif

    // Update counters:
    for (auto& c : umon_.hit_counters)
        c >>= 1;
    umon_.total_misses >>= 1;

#if defined(UCP_ENABLE_LOGGER)
    mcp_logger_.flush();
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_on_probe(const Transaction& trans)
{
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
        umon_fill(trans);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::umon_fill(const Transaction& trans)
{
    auto victim = umon_.atd.fill(trans, [] (const cset_type&, const CacheEntry&) {});

    size_t channel = dram_channel(trans.address);
    size_t idx = cache_set_index<IMPL>(victim.address) >> ilog2(UMON_SET_MODULUS);

    // Handle writeback:
    if (write_mode_[channel])
    {
        size_t x = wb_counters_[idx];
        x = std::clamp(x, static_cast<size_t>(0), static_cast<size_t>(IMPL::NUM_WAYS));
        ++wb_buckets_[x];

        wb_counters_[idx] = 0;
    }
    else if (victim.valid && victim.dirty)
    {
        ++wb_counters_[idx];
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
