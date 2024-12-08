'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import GEN_DIR, AUTOGEN_HEADER

####################################################################
####################################################################

def declare_cache_type(cfg, typename: str, next_typename: str) -> str:
    sets, ways, repl = cfg['sets'], cfg['ways'], cfg['replacement_policy']
    num_mshr, num_rw_ports, latency = cfg['num_mshr'], cfg['num_rw_ports'], cfg['latency']
    rq_size, wq_size, pq_size = cfg['read_queue_size'], cfg['write_queue_size'], cfg['prefetch_queue_size']
    
    write_allocate = 'true' if (cfg['mode'] == 'WRITE_ALLOCATE') else 'false'
    invalidate_on_hit = 'true' if (cfg['mode'] == 'INVALIDATE_ON_HIT') else 'false'
    next_is_invalidate_on_hit = 'true' if (cfg['mode'] == 'NEXT_IS_INVALIDATE_ON_HIT') else 'false'

    cache_decl =\
f'''
struct {typename} : public CacheControl<{typename}, Cache<{sets},{ways},CacheReplPolicy::{repl}>, {next_typename}>
{{
    constexpr static size_t RQ_SIZE = {rq_size};
    constexpr static size_t WQ_SIZE = {wq_size};
    constexpr static size_t PQ_SIZE = {pq_size};

    constexpr static size_t NUM_MSHR = {num_mshr};
    constexpr static size_t NUM_RW_PORTS = {num_rw_ports};
    constexpr static size_t CACHE_LATENCY = {latency};

    constexpr static bool WRITE_ALLOCATE = {write_allocate};
    constexpr static bool INVALIDATE_ON_HIT = {invalidate_on_hit};
    constexpr static bool NEXT_IS_INVALIDATE_ON_HIT = {next_is_invalidate_on_hit};

    {typename}(std::string name, CacheControl::next_ptr& n)
        :CacheControl(name, n)
    {{}}
}};
'''
    return cache_decl

####################################################################
####################################################################

def write(cfg, build):
    wr = open(f'{GEN_DIR}/{build}/memsys.h', 'w')
    wr.write(
f'''{AUTOGEN_HEADER}

#ifndef MEMSYS_h
#define MEMSYS_h

#include "cache/control.h"
#include "dram.h"

#include <memory>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
''')
    caches = ['LLC', 'L2', 'L1d', 'L1i',
              'L2TLB', 'iTLB', 'dTLB']
    # First write cache classes
    cache_typenames = ['LLCache', 'L2Cache', 'L1DCache', 'L1ICache',
                        'L2TLB', 'ITLB', 'DTLB']
    next_idx = [-1, 0, 1, 1,
                1, 4, 4]
    # Do in reverse order so `next_typename` is declared.
    for (i, c) in enumerate(caches):
        typename = cache_typenames[i]
        ii = next_idx[i]
        if ii == -1:
            next_typename = 'DRAM'
        else:
            next_typename = cache_typenames[ii]
            if cfg[caches[ii]]['mode'] == 'INVALIDATE_ON_HIT':
                cfg[c]['mode'] = 'NEXT_IS_INVALIDATE_ON_HIT'
        wr.write(declare_cache_type(cfg[c], typename, next_typename))
    wr.write(
'''

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
''')
    wr.close()

####################################################################
####################################################################

