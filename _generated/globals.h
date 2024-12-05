// THIS FILE IS AUTO-GENERATED BY `config.py` -- EDIT AT YOUR OWN RISK

#ifndef GLOBALS_h
#define GLOBALS_h

#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern uint64_t GL_CYCLE;
extern uint64_t GL_DRAM_CYCLE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

using core_ptr = std::unique_ptr<Core>;
using core_array_t = std::array<core_ptr, NUM_THREADS>;
using os_ptr = std::unique_ptr<OS>;
using llc_ptr = cache_ptr<LLCache>;
using dram_ptr = std::unique_ptr<DRAM>;

extern core_array_t GL_CORES;
extern os_ptr       GL_OS;
extern llc_ptr      GL_LLC;
extern dram_ptr     GL_DRAM;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
sim_init(std::vector<std::string> traces)
{
    for (size_t i = 0; i < NUM_THREADS; i++) {
        if (traces.size() == 1)
            GL_CORES[i] = core_ptr(new Core(traces[0]));
        else
            GL_CORES[i] = core_ptr(new Core(traces[i]));
    }

    GL_DRAM = dram_ptr(new DRAM(4.0, 2.4, 4800));
    GL_LLC = llc_ptr(new LLCache(GL_DRAM));
    GL_OS = os_ptr(new OS);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif