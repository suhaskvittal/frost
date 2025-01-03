/*
 *  author: Suhas Vittal
 *  date:   31 December 2024
 * */

#include "memsys.h"

#include "util/stats/cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
print_llc_stats(std::ostream& out)
{
    out << BAR << "\n";
    print_stat(out, "LLC", "WRITEBACKS", GL_LLC->s_writebacks_);
    print_stat(out, "LLC", "WB_NEXT_LINE_IN_$", GL_LLC->s_dirty_victim_adj_lines_);
    print_stat(out, "LLC", "WB_NEXT_LINE_IN_$_DIRTY", GL_LLC->s_dirty_victim_adj_lines_also_dirty_);

    if constexpr (LLCache::WRITEBACK_MODE == CacheWBMode::EAGER)
        print_stat(out, "LLC", "EAGER_WRITEBACKS", GL_LLC->s_eager_writebacks_);

    if constexpr (LLCache::WRITEBACK_MODE == CacheWBMode::VIRTUAL_WRITE_QUEUE)
        print_stat(out, "LLC", "SCHEDULED_WRITEBACKS", GL_LLC->s_scheduled_writebacks_);

    if constexpr (LLCache::WRITEBACK_MODE == CacheWBMode::NEXT_LINE)
    {
        double mean_next_line_lru_pos = mean(GL_LLC->s_tot_next_line_lru_pos_, GL_LLC->s_tot_next_lines_);

        print_stat(out, "LLC", "EAGER_WRITEBACKS", GL_LLC->s_eager_writebacks_);
        print_stat(out, "LLC", "E_NEXT_LINE_WAY", mean_next_line_lru_pos);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
