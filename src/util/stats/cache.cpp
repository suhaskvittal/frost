/*
 *  author: Suhas Vittal
 *  date:   31 December 2024
 * */

#include "globals.h"
#include "memsys.h"

#include "util/stats/cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
print_llc_stats(std::ostream& out)
{
    out << BAR << "\n";

    print_stat(out, "LLC", "READS", vec_sum(GL_LLC->s_reads_));
    print_stat(out, "LLC", "WRITES", vec_sum(GL_LLC->s_writes_));
    print_stat(out, "LLC", "EVICTIONS", GL_LLC->s_evictions_);
    print_stat(out, "LLC", "WRITEBACKS", GL_LLC->s_writebacks_);

    if (repl_uses_set_dueling(LLCache::REPL))
    {
        out << "\n";
        print_stat(out, "LLC", "POL_ONE_FILLS", GL_LLC->s_dueling_pol1_installs_);
        print_stat(out, "LLC", "POL_TWO_FILLS", GL_LLC->s_dueling_pol2_installs_);
    }
    
    if (LLCache::WRITEBACK_MODE != CacheWBMode::NORMAL)
    {
        out << "\n";
        print_stat(out, "LLC", "EAGER_WRITEBACKS", GL_LLC->s_eager_writebacks_);
        if (LLCache::WRITEBACK_MODE == CacheWBMode::SAME_SET_ROW_HARVEST)
        {
            for (size_t i = 0; i < GL_LLC->s_ssrh_tot_lru_pos_.size(); i++)
            {
                double elru_pos = mean(GL_LLC->s_ssrh_tot_lru_pos_[i], GL_LLC->s_ssrh_num_harvests_[i]);
                print_stat(out, "LLC", "SSRH_WB" + std::to_string(i+1) + "_LRU_POSITION", elru_pos);
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
