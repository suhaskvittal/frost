/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "constants.h"
#include "globals.h"
#include "memsys_decl.h"

#include "core.h"
#include "dram.h"
#include "os.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t GL_CYCLE = 0;

core_array_t GL_CORES;
os_ptr       GL_OS;
llc_ptr      GL_LLC;
dram_ptr     GL_DRAM;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
