'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import GEN_DIR, AUTOGEN_HEADER

####################################################################
####################################################################

def declare_cache_type(cfg, typename: str, next_typename: str, write_alloc=False) -> str:
    sets, ways, repl = cfg['sets'], cfg['ways'], cfg['replacement_policy']
    
    sets =          cfg['sets']
    ways =          cfg['ways']
    repl =          cfg['replacement_policy']
    rq_size =       cfg['read_queue_size']
    wq_size =       cfg['write_queue_size']
    pq_size =       cfg['prefetch_queue_size']
    latency =       cfg['latency']
    num_mshr =      cfg['num_mshr']
    wbq_size =      cfg['writeback_queue_size']
    fq_size =       cfg['fill_queue_size']
    r_ports =       cfg['read_ports']
    w_ports =       cfg['write_ports']
    f_ports =       cfg['fill_ports']
    wb_mode =       cfg['writeback_mode']
    cache_type =    cfg['cache_type']

    write_alloc = str(write_alloc).lower()

    cache_decl =\
f'''
struct {typename} : public {cache_type}<{typename}, {sets}, {ways}, {next_typename}>
{{
    using parent_type = {cache_type}<{typename}, {sets}, {ways}, {next_typename}>;

    constexpr static size_t NUM_SETS =         {sets};
    constexpr static size_t NUM_WAYS =         {ways};
    constexpr static CacheReplPolicy REPL =    CacheReplPolicy::{repl};

    constexpr static size_t RQ_SIZE =          {rq_size};
    constexpr static size_t WQ_SIZE =          {wq_size};
    constexpr static size_t PQ_SIZE =          {pq_size};

    constexpr static uint64_t CACHE_LATENCY =  {latency};
    constexpr static size_t NUM_MSHR =         {num_mshr};
    constexpr static size_t WB_QUEUE_SIZE =    {wbq_size};
    constexpr static size_t FILL_QUEUE_SIZE =  {fq_size};

    constexpr static size_t NUM_READ_PORTS =   {r_ports};
    constexpr static size_t NUM_WRITE_PORTS =  {w_ports};
    constexpr static size_t NUM_FILL_PORTS =   {f_ports};

    constexpr static bool WRITE_ALLOCATE = {write_alloc};

    constexpr static CacheWBMode WRITEBACK_MODE = CacheWBMode::{wb_mode};

    using parent_type::{cache_type};
}};
'''
    return cache_decl

####################################################################
####################################################################

def write(cfg, build):
    sim_model = cfg['SYSTEM']['model']

    ptw_inc = ''
    if sim_model == 'complex':
        ptw_inc = '#include \"complex_model/os/ptw.h\"\n'

    wr = open(f'{GEN_DIR}/{build}/memsys.h', 'w')
    wr.write(
f'''{AUTOGEN_HEADER}

#ifndef MEMSYS_h
#define MEMSYS_h

#include "cache.h"
#include "dram.h"
{ptw_inc}

#include "cache/other_impl/all.h"

#include <memory>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
''')
    cache_typenames = {
        'L1i': 'L1ICache',
        'L1d': 'L1DCache',
        'L2': 'L2Cache',
        'LLC': 'LLCache',
        'iTLB': 'ITLB',
        'dTLB': 'DTLB',
        'L2TLB': 'L2TLB'
    }

    if sim_model == 'complex':
        caches = ['LLC', 'L2', 'L1d', 'L1i', 'L2TLB', 'iTLB', 'dTLB']
        # First write cache classes
        next_idx = ['DRAM', 0, 1, 1, 'PageTableWalker', 4, 4]
    elif sim_model == 'simple':
        caches = ['LLC']
        next_idx = ['DRAM']
    else:
        print(f'config/memsys: unsupported sim model: {sim_model}')
        exit(1)

    # Do in reverse order so `next_typename` is declared.
    for (i, c) in enumerate(caches):
        typename = cache_typenames[c]
        ii = next_idx[i]
        if type(ii) is str:
            next_typename = ii
        else:
            next_typename = cache_typenames[caches[ii]]
            if cfg[caches[ii]]['operate_mode'] == 'INVALIDATE_ON_HIT':
                cfg[c]['operate_mode'] = 'NEXT_IS_INVALIDATE_ON_HIT'
        wr.write(declare_cache_type(cfg[c], typename, next_typename, write_alloc=(typename=='L1d')))
    wr.write(
'''

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
''')
    wr.close()

####################################################################
####################################################################

