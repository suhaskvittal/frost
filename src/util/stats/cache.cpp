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

void
print_llc_stats(std::ostream& out)
{
    out << BAR << "\n";
    print_stat(out, "LLC", "READS", GL_LLC->io_->s_reads_);
    print_stat(out, "LLC", "WRITES", GL_LLC->io_->s_writes_);

    print_stat(out, "LLC", "LOAD_BYPASSES", GL_LLC->s_bypasses_);
    print_stat(out, "LLC", "WRITEBACK_BYPASSES", GL_LLC->s_writeback_bypasses_);
    print_stat(out, "LLC", "EVICTIONS", GL_LLC->s_evictions_);
    print_stat(out, "LLC", "DEAD_BLOCK_PREDICTS", GL_LLC->s_dead_block_predicts_);
    print_stat(out, "LLC", "EVICTIONS_DEAD_BLOCKS", GL_LLC->s_evictions_due_to_dead_block_predictor_);
    print_stat(out, "LLC", "WRITEBACKS", GL_LLC->s_writebacks_);

    if (LLCache::cache_type::uses_set_dueling())
    {
        print_stat(out, "LLC", "SET_DUELING_POL1_INSTALLS", GL_LLC->cache_->s_dueling_pol1_installs_);
        print_stat(out, "LLC", "SET_DUELING_POL2_INSTALLS", GL_LLC->cache_->s_dueling_pol2_installs_);
    }

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
