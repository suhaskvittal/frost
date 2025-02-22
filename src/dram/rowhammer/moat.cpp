/*
 *  author: Suhas Vittal
 *  date:   22 February 2025
 * */

#include "dram/rowhammer/moat.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
MOAT::update_on_command(const DRAMCommand& cmd)
{
    if (!cmd.is_pre() && !cmd.counter_update)
        return;

    size_t r = dram_row(cmd.address);
        
    auto& c = prac_.ctrs[r];
    ++c;

    // Check if `c` is eligible for tracking:
    update_tracked_row(r, c);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
MOAT::handle_refresh(size_t row_start, size_t row_end)
{
    auto& ctrs = prac_.ctrs;
    auto begin = ctrs.begin() + row_start,
         end = ctrs.begin() + row_end;

    // Clear PRAC:
    std::fill(begin, end, 0);

    // Check if a tracked row was cleared:
    if (tracked_row_.has_value())
    {
        size_t r = tracked_row_.value();
        if (r >= row_start && r < row_end)
            tracked_row_.clear();
        else if (mitig_trefi_counter_ == OPT_AGGRESSOR_MITIGATION_RATE)
            do_aggressor_mitigation(r);

        // Increment `mitig_trefi_counter_`;
        ++mitig_trefi_counter_;
        if (mitig_trefi_counter_ == OPT_AGGRESSOR_MITIGATION_RATE)
            mitig_trefi_counter_ = 0;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
MOAT::handle_rfm()
{
    if (!tracked_row_.has_value())
        return;

    size_t r = tracked_row_.value();
    do_aggressor_mitigation(r);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
MOAT::do_aggressor_mitigation(size_t r)
{
    ++s_mitigations_;

    prac_.ctrs[r] = 0;
    tracked_row_.clear();

    // Need to increment victim counters: assume blast radius of 2:
    for (int b = 0; b < 2; b++)
    {
        ssize_t lwr = static_cast<ssize_t>(r) - b;
        ssize_t upp = static_cast<ssize_t>(r) + b;

        if (lwr >= 0)
            ++prac_.ctrs[lwr];
        if (upp < DRAM_ROWS)
            ++prac_.ctrs[upp];
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
