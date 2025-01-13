/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "constants.h"
#include "memsys.h"
#include "globals.h"
#include "sim.h"

#include "dram/cmd_args.h"

#include "util/argparse.h"
#include "util/stats/cache.h"
#include "util/timer.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

static Timer timer;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t GL_CYCLE = 0;

core_array_t GL_CORES;
os_ptr       GL_OS;
llc_ptr      GL_LLC;
dram_ptr     GL_DRAM;

std::string OPT_TRACE_FILE;
uint64_t OPT_INST_SIM;
uint64_t OPT_INST_WARMUP;
/*
 * DRAM parameters:
 * */
double OPT_DRAM_LOW_WATERMARK;
double OPT_DRAM_HIGH_WATERMARK;
uint64_t OPT_DRAM_WRITE_SYNC_COUNT;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class ARCH_PTR> inline uint64_t
tick_and_measure(ARCH_PTR& x)
{
    timer.start();
    x->tick();
    return timer.end();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    std::ios_base::sync_with_stdio(false);
    ArgParseResult ARGS(argc, argv,
            { // Required
                "trace"
            },
            { // Optional
                {"w", "Number of warmup instructions", "10000000"},
                {"s", "Number of instructions to simulate", "10000000"},
                {"dram_wm_low", "DRAM Low Watermark", "0.0"},
                {"dram_wm_high", "DRAM High Watermark", "1.0"},
                {"dram_wsync_count", "In DRAM_WRITE_POLICY = SYNC, number of writes to drain (per bank)", "0"}
            });
    ARGS("trace", OPT_TRACE_FILE);
    ARGS("w", OPT_INST_WARMUP);
    ARGS("s", OPT_INST_SIM);
    ARGS("dram_wm_low", OPT_DRAM_LOW_WATERMARK);
    ARGS("dram_wm_high", OPT_DRAM_HIGH_WATERMARK);
    ARGS("dram_wsync_count", OPT_DRAM_WRITE_SYNC_COUNT);

    sim_init();
    print_config(std::cout);

    std::cout << "WARMUP:\t";
    std::cout.flush();

    size_t curr_core_idx = 0;
    for (uint64_t i = 0; i < OPT_INST_WARMUP; i++)
    {
        if (i % 1'000'000 == 0)
        {
            std::cout << ".";
            std::cout.flush();
        }

        size_t ii = curr_core_idx;
        for (size_t j = 0; j < NUM_THREADS; j++)
        {
            GL_CORES[ii]->tick_warmup();
            fast_increment_and_mod_inplace<NUM_THREADS>(ii);
        }
        fast_increment_and_mod_inplace<NUM_THREADS>(curr_core_idx);
    }
    std::cout << "DONE\n";

    uint64_t time_in_core =0,
             time_in_llc =0,
             time_in_os,
             time_in_dram =0;

    bool all_done;
    do
    {
        print_progress(std::cout);

        time_in_dram += tick_and_measure(GL_DRAM);
        time_in_llc += tick_and_measure(GL_LLC);
        time_in_os += tick_and_measure(GL_OS);

        drain_llc_outgoing_queue();

        size_t ii = curr_core_idx;
        for (size_t i = 0; i < NUM_THREADS; i++)
        {
            auto& c = GL_CORES[ii];
            time_in_core += tick_and_measure(c);
            if (!c->done_ && c->finished_inst_num_ >= OPT_INST_SIM)
            {
                c->checkpoint_stats();
                c->done_ = true;
            }
            fast_increment_and_mod_inplace<NUM_THREADS>(ii);
        }
        fast_increment_and_mod_inplace<NUM_THREADS>(curr_core_idx);

        all_done = std::all_of(GL_CORES.begin(), GL_CORES.end(),
                        [] (const core_ptr& c)
                        {
                            return c->done_;
                        });
        ++GL_CYCLE;
    } while (!all_done);

    std::cout << "\n";
    for (size_t i = 0; i < NUM_THREADS; i++)
        GL_CORES[i]->print_stats(std::cout);
    print_llc_stats(std::cout);
    GL_DRAM->print_stats(std::cout);
    GL_OS->print_stats(std::cout);

    print_stat(std::cout, "TIME", "IN_CORE", time_in_core*1e-9);
    print_stat(std::cout, "TIME", "IN_OS", time_in_os*1e-9);
    print_stat(std::cout, "TIME", "IN_LLC", time_in_llc*1e-9);
    print_stat(std::cout, "TIME", "IN_DRAM", time_in_dram*1e-9);

    uint64_t total_time = time_in_core + time_in_os + time_in_llc + time_in_dram;

    print_stat(std::cout, "TIME", "TOTAL", total_time*1e-9);

    std::cout << BAR << "\n";

    return 0;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
