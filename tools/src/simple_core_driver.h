/*
 *  author: Suhas Vittal
 *  date:   24 January 2025
 * */

#ifndef SIMPLE_CORE_DRIVER_h
#define SIMPLE_CORE_DRIVER_h

#include "trace/fmt.h"
#include "trace/reader.h"

#include <cstddef>
#include <memory>
#include <string>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class CACHE_TYPE>
struct SimpleCoreDriver
{
    using tracereader_type = TraceReader<MTF>;
    using cache_ptr = std::unique_ptr<CACHE_TYPE>;

    uint64_t s_misses =0;
    uint64_t s_accesses =0;
    uint64_t s_inst =0;
    uint64_t s_fills =0;
    uint64_t s_writebacks =0;

    std::string      trace_file;
    tracereader_type trace_reader;
    
    cache_ptr cache;

    SimpleCoreDriver(std::string trace_file);
    /*
     * Reads next instruction in the trace and issues it to the
     * LLC.
     * */
    void step(bool warmup=false);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class CACHE_TYPE>
SimpleCoreDriver<CACHE_TYPE>::SimpleCoreDriver(std::string f)
    :trace_file(f),
    trace_reader(f),
    cache(new CACHE_TYPE)
{}

template <class CACHE_TYPE> void
SimpleCoreDriver<CACHE_TYPE>::step(bool warmup)
{
    // Get next instruction from trace reader.
    auto& fmt = trace_reader();
    if (trace_reader.eof_)
        return;
    // Get relevant data:
    uint64_t inst_num =0;
    bool is_write = false;
    uint64_t v_lineaddr = 0;

    memmove(&inst_num, fmt.inst_num, 5);
    memmove(&is_write, &fmt.is_write, 1);
    memmove(&v_lineaddr, fmt.v_lineaddr, 4);

    // Perform cache access:
    bool hit = is_write ? cache->mark(v_lineaddr, true) : cache->probe(v_lineaddr);
    if (!warmup && !is_write)
        ++s_accesses;
    if (!hit)
    {
        auto victim = cache->fill(v_lineaddr, 1, is_write);
        if (!warmup)
        {
            if (!is_write)
                ++s_misses;
            ++s_fills;
            if (victim.has_value() && victim.value().dirty)
                ++s_writebacks;
        }
    }
    s_inst = inst_num;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // SIMPLE_CORE_DRIVER_h
