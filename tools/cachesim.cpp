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

int main(int argc, char* argv[])
{
    std::string trace_file;
    uint64_t inst_sim;
    uint64_t inst_warmup;

    size_t cache_assoc, cache_size_kb;

    std::string miss_trace_file;

    ArgParseResult ARGS(argc, argv,
            {
                "trace"
            },
            {
                {"s", "number of instructions to simulate", "100000000"},
                {"w", "number of instructions to warmup", "0"},
                {"size_kb", "cache size", "2048"},
                {"assoc", "cache associativity", "16"},
                {"miss_trace", "file to record eviction record to", "NIL"}
            });
    ARGS("trace", trace_file);
    ARGS("s", inst_sim);
    ARGS("w", inst_warmup);

    ARGS("size_kb", cache_size_kb);
    ARGS("assoc", cache_assoc);

    ARGS("miss_trace", miss_trace_file);

    // cache initialization
    size_t cache_sets = (cache_size_kb*1024) / (cache_assoc*64);
    std::unique_ptr<SimpleCache> cache{new SimpleCache(cache_assoc, cache_sets)};

    SimpleCoreDriver driver(trace_file, std::move(cache));
    while (driver.s_inst < inst_warmup && !driver.trace_reader.eof_)
    {
        driver.step(true);
    }
    inst_warmup = driver.s_inst;

    // init miss trace:
    if (miss_trace_file != "NIL")
        driver.cache->start_recording_miss_trace(miss_trace_file);

    while (driver.s_inst-inst_warmup < inst_sim && !driver.trace_reader.eof_)
    {
        driver.step();
    }

    // Print stats:
    double miss_rate = mean(driver.s_misses, driver.s_accesses);
    double apki = mean(driver.s_accesses*1000, driver.s_inst);
    double mpki = mean(driver.s_misses*1000, driver.s_inst);

    print_stat(std::cout, "CACHE", "MISSES", driver.s_misses);
    print_stat(std::cout, "CACHE", "ACCESSES", driver.s_accesses);
    print_stat(std::cout, "CACHE", "FILLS", driver.s_fills);
    print_stat(std::cout, "CACHE", "WRITEBACKS", driver.s_writebacks);
    print_stat(std::cout, "CACHE", "MISS_RATE", miss_rate);
    print_stat(std::cout, "WORKLOAD", "APKI", apki);
    print_stat(std::cout, "WORKLOAD", "MPKI", mpki);
    print_stat(std::cout, "SIM", "INSTRUCTIONS", driver.s_inst - inst_warmup);
    
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
