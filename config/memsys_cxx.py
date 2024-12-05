'''
    author: Suhas Vittal
    date:   4 Decemeber 2024
'''

from .files import MEMSYS_CXX_FILE, AUTOGEN_HEADER

####################################################################
####################################################################

def write():
    l1i_type, l1d_type, l2_type, llc_type = 'L1ICache', 'L1DCache', 'L2Cache', 'LLCache'

    with open(MEMSYS_CXX_FILE, 'w') as wr:
        wr.write(
f'''{AUTOGEN_HEADER}

#include "memsys_decl.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

cache_ptr<{l1i_type}> GL_L1IC;
cache_ptr<{l1d_type}> GL_L1DC;
cache_ptr<{l2_type}> GL_L2C;
cache_ptr<{llc_type}> GL_LLC;

dram_ptr GL_DRAM;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
memsys_init()
{{
    // We have yet to write `DRAM` so TODO: pretend `dram_` is initialized.
    GL_LLC = std::make_unique<{llc_type}>(GL_DRAM);
    GL_L2C = std::make_unique<{l2_type}>(GL_LLC); 
    GL_L1IC = std::make_unique<{l1i_type}>(GL_L2C);
    GL_L1DC = std::make_unique<{l1d_type}>(GL_L2C);
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
''')

####################################################################
####################################################################
