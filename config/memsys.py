'''
    author: Suhas Vittal
    date:   4 December 2024
'''

from .files import MEMSYS_FILE, AUTOGEN_HEADER

####################################################################
####################################################################

def declare_cache_type(cfg, typename: str, next_typename: str) -> str:
    sets, ways, repl = cfg['sets'], cfg['ways'], cfg['replacement_policy']
    num_mshr, num_rw_ports = cfg['num_mshr'], cfg['num_rw_ports']
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

    constexpr static bool WRITE_ALLOCATE = {write_allocate};
    constexpr static bool INVALIDATE_ON_HIT = {invalidate_on_hit};
    constexpr static bool NEXT_IS_INVALIDATE_ON_HIT = {next_is_invalidate_on_hit};
}};
'''
    return cache_decl

####################################################################
####################################################################

def write(cfg):
    wr = open(MEMSYS_FILE, 'w')
    wr.write(
f'''{AUTOGEN_HEADER}

#ifndef MEMSYS_DECL_h
#define MEMSYS_DECL_h

#include "cache/control.h"
#include "dram.h"

#include <memory>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
''')
    caches = ['LLC', 'L2', 'L1d', 'L1i']
    # First write cache classes
    cache_typenames = ['LLCache', 'L2Cache', 'L1DCache', 'L1ICache']
    # Do in reverse order so `next_typename` is declared.
    for (i, c) in enumerate(caches):
        typename = cache_typenames[i]
        if c == 'LLC':
            next_typename = 'DRAM'
        else:
            next_typename = cache_typenames[i-1]
            if cfg[caches[i-1]]['mode'] == 'INVALIDATE_ON_HIT':
                cfg[c]['mode'] = 'NEXT_IS_INVALIDATE_ON_HIT'
        wr.write(declare_cache_type(cfg[c], typename, next_typename))
    wr.write(
'''
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class CACHE_TYPE>
using cache_ptr = std::unique_ptr<CACHE_TYPE>;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif
''')
    wr.close()

####################################################################
####################################################################
