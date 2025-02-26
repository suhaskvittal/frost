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
    print_stat(out, "LLC", "READ_FORWARDS", vec_sum(GL_LLC->s_read_forwards_));
    print_stat(out, "LLC", "WRITE_FORWARDS", vec_sum(GL_LLC->s_write_forwards_));
    print_stat(out, "LLC", "REWRITES", vec_sum(GL_LLC->s_rewrites_));
    print_stat(out, "LLC", "EVICTIONS", GL_LLC->s_evictions_);
    print_stat(out, "LLC", "WRITEBACKS", GL_LLC->s_writebacks_);

    print_stat(out, "LLC", "EOS_WRITE_OCCUPANCY", GL_LLC->write_occu());

    if (repl_uses_set_dueling(LLCache::REPL))
    {
        out << "\n";
        print_stat(out, "LLC", "POL_ONE_FILLS", GL_LLC->s_dueling_pol1_installs_);
        print_stat(out, "LLC", "POL_TWO_FILLS", GL_LLC->s_dueling_pol2_installs_);
    }
    
    if (LLCache::WRITEBACK_POLICY != CacheWritebackPolicy::NORMAL)
    {
        out << "\n";
        print_stat(out, "LLC", "EAGER_WRITEBACKS", GL_LLC->s_eager_writebacks_);
    }

    out << "\n";
    print_stat(out, "LLC", "BYPASSES", GL_LLC->s_bypasses_);
    print_stat(out, "LLC", "DEAD_BLOCK_EVICTIONS", GL_LLC->s_dead_block_evictions_);

    if (!std::is_same<LLCache::PARTITION_MANAGER_TYPE, NoPartitionManager>::value)
    {
        out << "\n";
        // Print out partitions for each core:
        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            double way_alloc = mean(GL_LLC->s_lifetime_way_alloc_[i], GL_LLC->s_total_way_allocs);
            print_stat(out, "LLC", "CORE_" + std::to_string(i) + "_MEAN_WAY_ALLOCATIONS", way_alloc);
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
