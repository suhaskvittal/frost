'''
    author: Suhas Vittal
    date:   5 December 2024
'''

from .files import DRAM_TIMING_FILE, AUTOGEN_HEADER

import math

####################################################################
####################################################################

def write(cfg):
    BL = int(cfg['DRAM']['BL'])
    dram_freq = float(cfg['DRAM']['frequency_ghz'])
    dram_type = cfg['DRAM']['dram_type']
    
    def ckcast(t_ns: float) -> int:
        return int(math.ceil(t_ns*dram_freq))

    if dram_type == '4800':
        CL = ckcast(16.0)
        CWL = CL-2
        tRCD = ckcast(16.0)
        tRP = ckcast(16.0)
        tRAS = ckcast(32.0)
        tRTP = max(12, ckcast(7.5))
        tWR = ckcast(30.0)

        tCCD_S = 8
        tCCD_S_WR = 8
        tCCD_S_WTR = CWL + BL//2 + max(4, ckcast(2.5));
        tCCD_S_RTW = tCCD_S_WTR

        tCCD_L = max(8, ckcast(5.0))
        tCCD_L_WR = max(32, ckcast(20.0))
        tCCD_L_WTR = CWL + BL//2 + max(16, ckcast(10.0))
        tCCD_L_RTW = tCCD_L_WTR

        tRRD_S = 8
        tRRD_L = max(8, ckcast(5.0))
        tFAW = max(32, ckcast(13.333))
        tRFC = ckcast(410.0)
        tREFI = ckcast(32e6/8192.0)
    else:
        print(f'Unsupported dram type: {dram_type}')
        exit(1)

    with open(DRAM_TIMING_FILE, 'w') as wr:
        wr.write(
f'''{AUTOGEN_HEADER}

#ifndef DRAM_TIMING_h
#define DRAM_TIMING_h

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr uint64_t CL = {CL};
constexpr uint64_t CWL = {CWL};
constexpr uint64_t tRCD = {tRCD};
constexpr uint64_t tRP = {tRP};
constexpr uint64_t tRAS = {tRAS};
constexpr uint64_t tRTP = {tRTP};
constexpr uint64_t tWR = {tWR};

constexpr uint64_t tCCD_S = {tCCD_S};
constexpr uint64_t tCCD_S_WR = {tCCD_S_WR};
constexpr uint64_t tCCD_S_WTR = {tCCD_S_WTR};
constexpr uint64_t tCCD_S_RTW = {tCCD_S_RTW};

constexpr uint64_t tCCD_L = {tCCD_L};
constexpr uint64_t tCCD_L_WR = {tCCD_L_WR};
constexpr uint64_t tCCD_L_WTR = {tCCD_L_WTR};
constexpr uint64_t tCCD_L_RTW = {tCCD_L_RTW};

constexpr uint64_t tRRD_S = {tRRD_S};
constexpr uint64_t tRRD_L = {tRRD_L};
constexpr uint64_t tFAW = {tFAW};

constexpr uint64_t tRFC = {tRFC};
constexpr uint64_t tREFI = {tREFI};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_TIMING_h

''')

####################################################################
####################################################################
