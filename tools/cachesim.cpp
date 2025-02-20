/*
 *  author: Suhas Vittal
 *  date:   24 January 2025
 * */

#include <cstdint>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "simple_cache.h"
#include "simple_core_driver.h"
#include "util/argparse.h"
#include "util/stats.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_cache_stats(std::string cache_name, const SimpleCache& c, uint64_t inst)
{
    double miss_rate = mean(c.s_misses_, c.s_accesses_);
    double mpki = mean(c.s_misses_, inst) * 1000.0;

    print_stat(std::cout, cache_name, "ACCESSES", c.s_accesses_);
    print_stat(std::cout, cache_name, "MISSES", c.s_misses_);
    print_stat(std::cout, cache_name, "MISS_RATE", miss_rate);
    print_stat(std::cout, cache_name, "MPKI", mpki);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    std::string trace_file;
    uint64_t inst_sim;

    size_t cache_assoc, cache_size_kb;

    std::string miss_trace_file;

    ArgParseResult ARGS(argc, argv,
            {
                "trace"
            },
            {
                {"s", "number of instructions to simulate", "100000000"},
                {"size_kb", "cache size", "2048"},
                {"assoc", "cache associativity", "16"},
                {"miss_trace", "file to record eviction record to", "NIL"},
                {"memento", "Testing \"memento\" prefetcher for LLC", ""}
            });
    ARGS("trace", trace_file);
    ARGS("s", inst_sim);

    ARGS("size_kb", cache_size_kb);
    ARGS("assoc", cache_assoc);

    ARGS("miss_trace", miss_trace_file);

    // cache initialization
    size_t cache_sets = (cache_size_kb*1024) / (cache_assoc*64);

    SimpleCache l1i_cache(8, 64);   // 32 KB
    SimpleCache l1d_cache(12, 64);  // 48 KB
    SimpleCache l2_cache(8, 512);   // 256 KB

    SimpleCache l3_cache(cache_assoc, cache_sets);

    // Setup any optional policies:
    ARGS("memento", l3_cache.enable_memento_test_);

    if (miss_trace_file != "NIL")
        l3_cache.start_recording_miss_trace(miss_trace_file);

    uint64_t total_inst = inst_sim;

    SimpleCoreDriver driver(trace_file, std::move(l1i_cache), std::move(l1d_cache), std::move(l2_cache), std::move(l3_cache));

    while (inst_sim-- && !driver.trace_reader.eof_)
    {
        driver.step();
    }

    std::cout << "============================================================="
                << "\nTRACE = " << trace_file
                << "\n============================================================="
                << "\n";

    print_cache_stats("L1I$", driver.l1i_cache, total_inst);
    print_cache_stats("L1D$", driver.l1d_cache, total_inst);
    print_cache_stats("L2$", driver.l2_cache, total_inst);
    print_cache_stats("L3$ (LLC)", driver.l3_cache, total_inst);
    
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
