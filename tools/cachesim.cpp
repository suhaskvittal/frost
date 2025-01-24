/*
 *  author: Suhas Vittal
 *  date:   24 January 2025
 * */

#include <cstdint>

uint64_t GL_CYCLE =0;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "cache.h"
#include "simple_core_driver.h"
#include "util/argparse.h"
#include "util/stats.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#if !defined(CACHE_SIZE_MB)
#define CACHE_SIZE_MB     2048
#endif

#if !defined(CACHE_ASSOC)
#define CACHE_ASSOC       16
#endif

#if !defined(CACHE_REPL)
#define CACHE_REPL        CacheReplPolicy::LRU
#endif

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t LINESIZE = 64;
constexpr size_t CACHE_SETS = (CACHE_SIZE_MB*1024*1024)/(CACHE_ASSOC*LINESIZE);

using DefinedCache = Cache<CACHE_SETS, CACHE_ASSOC, CACHE_REPL>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    std::string trace_file;
    uint64_t inst_sim;
    uint64_t inst_warmup;

    ArgParseResult ARGS(argc, argv,
            {
                "trace"
            },
            {
                {"s", "number of instructions to simulate", "100000000"},
                {"w", "number of instructions to warmup", "0"}
            });
    ARGS("trace", trace_file);
    ARGS("s", inst_sim);
    ARGS("w", inst_warmup);

    using driver_type = SimpleCoreDriver<DefinedCache>;

    driver_type driver(trace_file);
    while (driver.s_inst < inst_warmup && !driver.trace_reader.eof_)
    {
        driver.step(true);
        ++GL_CYCLE;
    }
    inst_warmup = driver.s_inst;

    while (driver.s_inst-inst_warmup < inst_sim && !driver.trace_reader.eof_)
    {
        driver.step();
        ++GL_CYCLE;
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
