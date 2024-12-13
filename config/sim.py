'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import GEN_DIR, AUTOGEN_HEADER

####################################################################
####################################################################

BANK_TIMINGS = [
    'CL','CWL','tRCD','tRP','tRAS','tRTP','tWR'
]

CHANNEL_TIMINGS = [
    'tCCD_S', 'tCCD_S_WR', 'tCCD_S_WTR', 'tCCD_S_RTW',
    'tCCD_L', 'tCCD_L_WR', 'tCCD_L_WTR', 'tCCD_L_RTW',
    'tFAW', 'tRFC', 'tREFI'
]

####################################################################
####################################################################

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
        latency = ccfg['latency']
        rq = ccfg['read_queue_size']
        wq = ccfg['write_queue_size']
        pq = ccfg['prefetch_queue_size']

        calls[c] = f'{size_kb}, {sets}, {ways}, \"{repl}\", {num_mshr}, {num_rw}, {latency}, {rq}, {wq}, {pq}'
    return calls

####################################################################
####################################################################

def write(cfg, build):
    sim_model = cfg['SYSTEM']['model']

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
    dram_page_policy = cfg['DRAM']['page_policy']
    dram_am = cfg['DRAM']['address_mapping']

    # OS params:
    ptwc_params = ''
    if sim_model == 'complex':
        pt_levels = int(cfg['OS']['levels'])
        for i in range(1, pt_levels):
            sw = cfg['OS'][f'ptwc_{i}_sw']
            dat = sw.split(':')
            sets, ways = int(dat[0]), int(dat[1])
            if i > 1:
                ptwc_params += ', '
            ptwc_params += f'{{{sets},{ways}}}'
        ptwc_params = f'{{ {ptwc_params} }}'

    # Progress params:
    epoch_size = 50_000_000
    if sim_model == 'complex':
        epoch_size = 5_000_000
    dot_size = epoch_size // 50

    with open(f'{GEN_DIR}/{build}/sim.h', 'w') as wr:
        wr.write(
fr'''{AUTOGEN_HEADER}

#ifndef SIM_h
#define SIM_h

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

// Include specific core model files here:
#include "{sim_model}_model/core.h"
#include "{sim_model}_model/os.h"
#include "dram.h"

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
    with open(f'{GEN_DIR}/{build}/sim.cpp', 'w') as wr:
        wr.write(
fr'''{AUTOGEN_HEADER}

#include "dram_timing.h"
#include "sim.h"

#include "constants.h"
#include "globals.h"
#include "memsys.h"

#include "dram/address.h"

#include <sstream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
sim_init(void)
{{
    GL_DRAM = dram_ptr(new DRAM({cpu_freq}, {dram_freq}));
    GL_LLC = llc_ptr(new LLCache("LLC", GL_DRAM));
    for (size_t i = 0; i < NUM_THREADS; i++)
        GL_CORES[i] = core_ptr(new Core(i, OPT_TRACE_FILE));
    GL_OS = os_ptr(new OS({ptwc_params}));
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class T> inline void
list(std::ostream& out, std::string_view stat, T x)
{{
    out << std::setw(32) << std::left << stat << std::setw(16) << std::left << x << "\n";
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
    size_t latency,
    size_t rq,
    size_t wq,
    size_t pq)
{{
    std::string qstr = std::to_string(rq) + ":" + std::to_string(wq) + ":" + std::to_string(pq);
    out << std::setw(12) << std::left << name
        << std::setw(12) << std::left << (size_kb == 0 ? "N/A" : std::to_string(size_kb))
        << std::setw(8) << std::left << sets
        << std::setw(8) << std::left << ways
        << std::setw(8) << std::left << repl
        << std::setw(8) << std::left << num_mshr
        << std::setw(8) << std::left << num_ports
        << std::setw(12) << std::left << latency
        << std::setw(12) << std::left << qstr
        << "\n";
}}

void
list_dram(std::ostream& out, std::string_view stat, uint64_t ck)
{{
    double time_ns = ck * {tCK};
    out << std::setw(24) << std::left << stat
        << std::setw(12) << std::left << std::setprecision(3) << time_ns
        << std::setw(12) << std::left << ck
        << "\n";
}}

void
print_config(std::ostream& out)
{{
    out << "\n" << BAR << "\n";
    
    list(out, "TRACE", OPT_TRACE_FILE);
    list(out, "INST_SIM", fmt_bignum(OPT_INST_SIM));
    list(out, "INST_WARMUP", fmt_bignum(OPT_INST_WARMUP));
    list(out, "NUM_THREADS", NUM_THREADS);
    list(out, "CORE_FETCH_WIDTH", CORE_FETCH_WIDTH);
    list(out, "CORE_ROB_SIZE", CORE_ROB_SIZE);
    list(out, "CORE_FTB_SIZE", CORE_FTB_SIZE);

    // List cache parameters.
    out << BAR << "\n"
        << std::setw(12) << std::left << "CACHE"
        << std::setw(12) << std::left << "SIZE_KB"
        << std::setw(8) << std::left << "SETS"
        << std::setw(8) << std::left << "WAYS"
        << std::setw(8) << std::left << "REPL"
        << std::setw(8) << std::left << "MSHR"
        << std::setw(8) << std::left << "PORTS"
        << std::setw(12) << std::left << "LATENCY"
        << std::setw(12) << std::left << "RQ:WQ:PQ"
        << "\n" << BAR << "\n";
    list_cache_params(out, "L1I$", {cache_params['L1i']});
    list_cache_params(out, "L1D$", {cache_params['L1d']});
    list_cache_params(out, "L2$", {cache_params['L2']});
    list_cache_params(out, "LLC", {cache_params['LLC']});

    out << BAR << "\n";
    
    list(out, "DRAM_FREQUENCY", "{dram_freq}");
    list(out, "DRAM_tCK", "{tCK:.5f}");
    list(out, "DRAM_PAGE_POLICY", "{dram_page_policy}");
    list(out, "DRAM_ADDRESS_MAPPING", "{dram_am}");
    print_address_mapping(out);
    out << "\n"
        << std::setw(24) << std::left << "DRAM_TIMING"
        << std::setw(12) << std::left << "ns"
        << std::setw(12) << std::left << "nCK"
        << "\n" << BAR << "\n";
    {bank_timing_calls}
    out << "\n";
    {channel_timing_calls}
    out << BAR << "\n\n";
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
print_progress(std::ostream& out)
{{
    if (GL_CYCLE % {dot_size} == 0) {{
        if (GL_CYCLE % {epoch_size} == 0) {{
            out << "\nCYCLE = " << std::setw(4) << std::left << fmt_bignum(GL_CYCLE)
                << "[ INST:";
            for (size_t i = 0; i < NUM_THREADS; i++)
                out << std::setw(7) << std::right << fmt_bignum(GL_CORES[i]->finished_inst_num_);
            out << " ]\n\tprogress: ";
        }}
        out << ".";
        out.flush();
    }}
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

std::string
fmt_bignum(uint64_t x)
{{
    std::string suffix;
    if (x < 1'000'000'000) {{
        x /= 1'000'000;
        suffix = "M";
    }} else {{
        x /= 1'000'000'000;
        suffix = "B";
    }}
    return std::to_string(x) + suffix;
}}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
''')
