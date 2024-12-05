'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import GLOBALS_FILE, AUTOGEN_HEADER

####################################################################
####################################################################

def write(cfg):
    cpu_freq = cfg['CORE']['frequency_ghz']
    dram_freq = cfg['DRAM']['frequency_ghz']
    dram_type = cfg['DRAM']['dram_type']

    with open(GLOBALS_FILE, 'w') as wr:
        wr.write(
f'''{AUTOGEN_HEADER}

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
{{
    for (size_t i = 0; i < NUM_THREADS; i++) {{
        if (traces.size() == 1)
            GL_CORES[i] = core_ptr(new Core(traces[0]));
        else
            GL_CORES[i] = core_ptr(new Core(traces[i]));
    }}

    GL_DRAM = dram_ptr(new DRAM({cpu_freq}, {dram_freq}, {dram_type}));
    GL_LLC = llc_ptr(new LLCache(GL_DRAM));
    GL_OS = os_ptr(new OS);
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
''')

####################################################################
####################################################################
