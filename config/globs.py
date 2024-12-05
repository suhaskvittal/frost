'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import GLOBALS_FILE, AUTOGEN_HEADER

####################################################################
####################################################################

def write(cfg):
    with open(GLOBALS_FILE, 'w') as wr:
        wr.write(
f'''{AUTOGEN_HEADER}

#ifndef GLOBALS_h
#define GLOBALS_h

#include "constants.h"

#include <array>
#include <cstdint>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern uint64_t GL_CYCLE;
extern uint64_t GL_DRAM_CYCLE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class Core;
class OS;
class LLCache;
class DRAM;

using core_ptr = std::unique_ptr<Core>;
using core_array_t = std::array<core_ptr, NUM_THREADS>;
using os_ptr = std::unique_ptr<OS>;
using llc_ptr = std::unique_ptr<LLCache>;
using dram_ptr = std::unique_ptr<DRAM>;

extern core_array_t GL_CORES;
extern os_ptr       GL_OS;
extern llc_ptr      GL_LLC;
extern dram_ptr     GL_DRAM;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
''')

####################################################################
####################################################################
