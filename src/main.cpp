/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "constants.h"
#include "memsys.h"
#include "globals.h"
#include "sim.h"

#include "util/argparse.h"

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
                {"s", "Number of instructions to simulate", "10000000"}
            });
    ARGS("trace", OPT_TRACE_FILE);
    ARGS("w", OPT_INST_WARMUP);
    ARGS("s", OPT_INST_SIM);

    sim_init();
    print_config(std::cout);

    std::cout << "WARMUP:\t";
    std::cout.flush();

    size_t curr_core_idx = 0;
    for (uint64_t i = 0; i < OPT_INST_WARMUP; i++) {
        if (i % 1'000'000 == 0) {
            std::cout << ".";
            std::cout.flush();
        }

        size_t ii = curr_core_idx;
        for (size_t j = 0; j < NUM_THREADS; j++) {
            GL_CORES[ii]->tick_warmup();
            fast_increment_and_mod_inplace<NUM_THREADS>(ii);
        }
        fast_increment_and_mod_inplace<NUM_THREADS>(curr_core_idx);
    }
    std::cout << "DONE\n";

    bool all_done;
    do {
        print_progress(std::cout);

        GL_DRAM->tick();
        GL_LLC->tick();
        GL_OS->tick();

        drain_llc_outgoing_queue();

        size_t ii = curr_core_idx;
        for (size_t i = 0; i < NUM_THREADS; i++) {
            auto& c = GL_CORES[ii];
            c->tick();
            if (!c->done_ && c->finished_inst_num_ >= OPT_INST_SIM) {
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
    GL_DRAM->print_stats(std::cout);
    GL_OS->print_stats(std::cout);

    return 0;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
