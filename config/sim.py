'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import SIM_H_FILE, SIM_CXX_FILE, AUTOGEN_HEADER

####################################################################
####################################################################

BL = 80
BANK_TIMINGS = [
    'CL','CWL','tRCD','tRP','tRAS','tRTP','tWR'
]

CHANNEL_SL_TIMINGS = [
    'tCCD%s', 'tCCD%s_WR', 'tCCD%s_WTR', 'tCCD%s_RTW', 'tRRD%s'
]

CHANNEL_TIMINGS = [
    'tFAW', 'tRFC', 'tREFI'
]

####################################################################
####################################################################

def BAR(n):
    return ''.join(['-' for _ in range(n)])

def get_cache_params(cfg, caches: list[str]) -> str:
    calls = {}
    for c in caches:
        ccfg = cfg[c]

        size_kb = '0' if 'size_kb' not in ccfg else ccfg['size_kb']
        ways = ccfg['ways']
        sets = ccfg['sets']
        repl = ccfg['replacement_policy']
        num_mshr = ccfg['num_mshr']
        num_rw = ccfg['num_rw_ports']
        rq = ccfg['read_queue_size']
        wq = ccfg['write_queue_size']
        pq = ccfg['prefetch_queue_size']

        calls[c] = f'{size_kb}, {ways}, {sets}, \"{repl}\", {num_mshr}, {num_rw}, {rq}, {wq}, {pq}'
    return calls

####################################################################
####################################################################

def write(cfg):
    cpu_freq = cfg['CORE']['frequency_ghz']
    dram_freq = cfg['DRAM']['frequency_ghz']
    tCK = 1.0/float(dram_freq)

    # Get list of parameter calls for listing out the config
    #
    # Cache params:
    cache_params = get_cache_params(cfg, ['L1i', 'L1d', 'L2', 'LLC'])

    # DRAM timings:
    bank_timing_calls = '\n\t'.join(f'list_dram(out, \"{t}\", {t});' for t in BANK_TIMINGS)
    channel_timing_calls = '\n\t'.join(f'list_dram(out, \"{t}\", {t});' for t in CHANNEL_TIMINGS)
    # SL timings are a bit more complicated to implement
    sl_timing_calls = []
    for t in CHANNEL_SL_TIMINGS:
        name, tS, tL = t % '_(S:L)', t % '_S', t % '_L'
        sl_timing_calls.append(f'list_dram_sl(out, \"{name}\", {tS}, {tL});')
    sl_timing_calls = '\n\t'.join(sl_timing_calls)

    with open(SIM_H_FILE, 'w') as wr:
        wr.write(
fr'''{AUTOGEN_HEADER}

#ifndef SIM_h
#define SIM_h

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 *  These are command line arguments. Should be set in the main file.
 * */
extern std::string OPT_TRACE_FILE;
extern uint64_t OPT_INST_SIM;
extern uint64_t OPT_INST_WARMUP;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 *  Helper functions defined in `sim.cpp`
 * */
void sim_init(void);
void print_config(std::ostream&);
void print_progress(std::ostream&);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

std::string fmt_bignum(uint64_t);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
''')
    with open(SIM_CXX_FILE, 'w') as wr:
        wr.write(
fr'''{AUTOGEN_HEADER}

#include "dram_timing.h"
#include "sim.h"

#include "constants.h"
#include "globals.h"
#include "memsys.h"

#include "core.h"
#include "dram.h"

#include <sstream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
sim_init(void)
{{
    for (size_t i = 0; i < NUM_THREADS: i++)
        GL_CORES[i] = core_ptr(new Core(i, OPT_TRACE_FILE));
    GL_DRAM = dram_ptr(new DRAM({cpu_freq}, {dram_freq}));
    GL_LLC = llc_ptr(new LLCache(GL_DRAM));
    GL_OS = os_ptr(new OS);
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class T> inline void
list(std::ostream& out, std::string_view stat, T x)
{{
    out << std::setw(32) << std::left << std::setw(16) << std::right << x << "\n";
}}

void
list_cache_params(
    std::ostream& out,
    std::string_view name,
    size_t size_kb,
    size_t sets,
    size_t ways,
    std::string_view repl,
    size_t num_mshr,
    size_t num_ports,
    size_t rq,
    size_t wq,
    size_t pq)
{{
    std::string qstr = std::to_string(rq) + ":" + std::to_string(wq) + ":" + std::to_string(pq);
    out << std::setw(12) << std::left << name
        << std::setw(6) << std::left << (size_kb == 0 ? "N/A" : size_kb)
        << std::setw(6) << std::left << sets
        << std::setw(6) << std::left << ways
        << std::setw(6) << std::left << repl
        << std::setw(6) << std::left << num_mshr
        << std::setw(6) << std::left << num_ports
        << std::setw(12) << std::left << qstr
        << "\n";
}}

void
list_dram(std::ostream& out, std::string_view stat, uint64_t ck)
{{
    double time_ns = ck * {tCK};
    out << std::setw(12) << std::left << stat
        << std::setw(12) << std::left << std::setprecision(3) << time_ns
        << std::setw(12) << std::left << ck
        << "\n";
}}

void
list_dram_sl(std::ostream& out, std::string_view stat, uint64_t ckS, uint64_t ckL)
{{
    double time_ns_S = ckS * {tCK},
           time_ns_L = ckL * {tCK};
    std::stringstream ss;
    ss << std::setprecision(1) << tS << ":" << std::setprecision(1) << tL;
    std::string nss = ss.str();
    std::string cks = std::to_string(ckS) + ":" + std::to_string(ckL);

    out << std::setw(12) << std::left << stat
        << std::setw(12) << std::left << nss
        << std::setw(12) << std::left << cks;
}}

void
print_config(std::ostream& out)
{{
    const std::string_view BAR = "{BAR(BL)}";

    out << "\n" << BAR << "\n\n";
    
    list(out, "TRACE", OPT_TRACE_FILE);
    list(out, "INST_SIM", fmt_bignum(OPT_INST_SIM));
    list(out, "INST_WARMUP", fmt_bignum(OPT_INST_WARMUP));
    list(out, "NUM_THREADS", NUM_THREADS);

    // List cache parameters.
    out << "\n" << BAR << "\n\n"
        << std::setw(12) << std::left << "CACHE"
        << std::setw(6) << std::left << "SIZE (kB)"
        << std::setw(6) << std::left << "SETS"
        << std::setw(6) << std::left << "WAYS"
        << std::setw(6) << std::left << "REPL"
        << std::setw(6) << std::left << "MSHR"
        << std::setw(6) << std::left << "PORTS"
        << std::setw(12) << std::left << "RQ:WQ:PQ"
        << "\n" << BAR << "\n";
    list_cache_params(out, "L1I$", {cache_params['L1i']});
    list_cache_params(out, "L1D$", {cache_params['L1d']});
    list_cache_params(out, "L2$", {cache_params['L2']});
    list_cache_params(out, "LLC", {cache_params['LLC']});

    out << "\n" << BAR << "\n\n"
        << "DRAM frequency = " << {dram_freq} << "GHz, tCK = " << {tCK:.5f} << "\n\n";
        << std::setw(12) << std::left << "DRAM TIMING"
        << std::setw(12) << std::left << "ns"
        << std::setw(12) << std::left << "nCK"
        << "\n" << BAR << "\n";
    {bank_timing_calls}
    out << "\n";
    {sl_timing_calls}
    out << "\n";
    {channel_timing_calls}
    out << "\n" << BAR << "\n\n";
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
print_progress(std::ostream& out)
{{
    if (GL_CYCLE % 50'000'000 == 0) {{
        out << "\nCYCLE = " << std::setw(4) << std::left << fmt_bignum(GL_CYCLE)
            << "[ INST:";
        for (size_t i = 0; i < NUM_THREADS: i++)
            out << std::setw(7) << std::right << fmt_bignum(GL_CORES[i]->finished_inst_num_);
        out << " ]\n\tprogress: ";
    }}
    out << ".";
    out.flush();
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

std::string
fmt_bignum(uint64_t x)
{{
    std::string suffix;
    if (num < 1'000'000'000) {{
        num /= 1'000'000;
        suffix = "M";
    }} else {{
        num /= 1'000'000'000;
        suffix = "B";
    }}
    return std::to_string(num) + suffix;
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
''')