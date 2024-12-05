'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import GEN_DIR, AUTOGEN_HEADER

####################################################################
####################################################################

def write(cfg, build):
    core_cfg = cfg['CORE']
    dram_cfg = cfg['DRAM']

    num_threads = core_cfg['num_threads']
    fetch_width = core_cfg['fetch_width']
    rob_size = core_cfg['rob_size']

    ch, ra, bg, ba, row, col = dram_cfg['channels'], dram_cfg['ranks'], dram_cfg['bankgroups'],\
                                dram_cfg['banks'], dram_cfg['rows'], dram_cfg['columns']
    BL = dram_cfg['BL']
    rq_size, wq_size, cmdq_size = dram_cfg['read_queue_size'], dram_cfg['write_queue_size'], dram_cfg['cmd_queue_size']
    page_policy = dram_cfg['page_policy']
    # Address mapping is a little less straightforward
    am = dram_cfg['address_mapping']
    if am[:3] == 'MOP':
        mop_size = int(am[3:])
        am_txt = f'#define DRAM_AM_MOP {mop_size}'
    else:
        am_txt = f'#define DRAM_AM_{am}'
    # Finally, write to file. 
    with open(f'{GEN_DIR}/{build}/constants.h', 'w') as wr:
        wr.write(
f'''{AUTOGEN_HEADER}

#ifndef CONSTANTS_h
#define CONSTANTS_h

#include <cstddef>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t LINESIZE = 64;
constexpr size_t PAGESIZE = 4096;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t NUM_THREADS = {num_threads};
constexpr size_t CORE_FETCH_WIDTH = {fetch_width};
constexpr size_t CORE_ROB_SIZE = {rob_size};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t DRAM_CHANNELS = {ch};
constexpr size_t DRAM_RANKS = {ra};
constexpr size_t DRAM_BANKGROUPS = {bg};
constexpr size_t DRAM_BANKS = {ba};
constexpr size_t DRAM_ROWS = {row};
constexpr size_t DRAM_COLUMNS = {col};

constexpr size_t DRAM_BURST_LENGTH = {BL};

constexpr size_t DRAM_RQ_SIZE = {rq_size};
constexpr size_t DRAM_WQ_SIZE = {wq_size};
constexpr size_t DRAM_CMDQ_SIZE = {cmdq_size};

constexpr size_t DRAM_SIZE_MB = DRAM_CHANNELS * DRAM_RANKS * DRAM_BANKGROUPS * DRAM_BANKS
                                * DRAM_ROWS * DRAM_COLUMNS * LINESIZE / (1024*1024);

#define DRAM_PAGE_POLICY DRAMPagePolicy::{page_policy}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

{am_txt}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
''')

####################################################################
####################################################################
