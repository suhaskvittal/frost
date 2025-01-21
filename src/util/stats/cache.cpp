/*
 *  author: Suhas Vittal
 *  date:   31 December 2024
 * */

#include "globals.h"
#include "memsys.h"

#include "cache/other_impl/bank_balanced_cache.h"
#include "util/stats/cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * Some of the stats need to be in template to avoid `if constexpr` evaluation when condition
 * is false.
 * */

template <class CACHE_TYPE> inline void
print_bank_balanced_cache_stats(std::ostream& out, std::unique_ptr<CACHE_TYPE>& c)
{
    if constexpr (is_bank_balanced_cache<CACHE_TYPE>::value)
    {
        print_stat(out, "LLC", "BB$_REPL_POLICY_1", c->s_repl_pol1_);
        print_stat(out, "LLC", "BB$_REPL_POLICY_2", c->s_repl_pol2_);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
print_llc_stats(std::ostream& out)
{
    out << BAR << "\n";
    print_stat(out, "LLC", "READS", GL_LLC->io_->s_reads_);
    print_stat(out, "LLC", "WRITES", GL_LLC->io_->s_writes_);

    print_stat(out, "LLC", "WRITEBACKS", GL_LLC->s_writebacks_);
    print_stat(out, "LLC", "WRITEBACK_NEXT_LINE_IN_$", GL_LLC->s_dirty_victim_adj_lines_);
    print_stat(out, "LLC", "WRITEBACK_NEXT_LINE_IN_$_DIRTY", GL_LLC->s_dirty_victim_adj_lines_also_dirty_);

    print_bank_balanced_cache_stats(out, GL_LLC->cache_);

    if constexpr (LLCache::WRITEBACK_MODE == CacheWBMode::EAGER)
        print_stat(out, "LLC", "EAGER_WRITEBACKS", GL_LLC->s_eager_writebacks_);

    if constexpr (LLCache::WRITEBACK_MODE == CacheWBMode::NEXT_LINE)
    {
        double mean_next_line_lru_pos = mean(GL_LLC->s_tot_next_line_lru_pos_, GL_LLC->s_tot_next_lines_);

        print_stat(out, "LLC", "EAGER_WRITEBACKS", GL_LLC->s_eager_writebacks_);
        print_stat(out, "LLC", "EXPECTED_NEXT_LINE_WAY", mean_next_line_lru_pos);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
