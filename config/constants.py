'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import CONSTANTS_FILE, AUTOGEN_HEADER

####################################################################
####################################################################

def write(cfg):
    dram_cfg = cfg['DRAM']
    
    # Write DRAM constants
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
    with open(CONSTANTS_FILE, 'w') as wr:
        wr.write(
f'''{AUTOGEN_HEADER}

#ifndef CONSTANTS_h
#define CONSTANTS_h

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t LINESIZE = 64;
constexpr size_t PAGESIZE = 4096;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMPagePolicy {{ OPEN, CLOSE }};

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

constexpr DRAMPagePolicy DRAM_PAGE_POLICY = DRAMPagePolicy::{page_policy};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

{am_txt}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
''')

####################################################################
####################################################################
