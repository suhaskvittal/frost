/*
 *  author: Suhas Vittal
 *  date:   24 January 2025
 * */

#ifndef SIMPLE_CORE_DRIVER_h
#define SIMPLE_CORE_DRIVER_h

#define TRACE_FORMAT_CTF

#include "instruction.h"
#include "trace/fmt.h"
#include "trace/reader.h"
#include "simple_cache.h"

#include <cstddef>
#include <memory>
#include <string>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct SimpleCoreDriver
{
public:
    using tracereader_type = TraceReader<CTF>;

    std::string      trace_file;
    tracereader_type trace_reader;
    
    SimpleCache l1i_cache;
    SimpleCache l1d_cache;
    SimpleCache l2_cache;
    SimpleCache l3_cache;

    SimpleCoreDriver(std::string trace_file, SimpleCache&&, SimpleCache&&, SimpleCache&&, SimpleCache&&);
    /*
     * Reads next instruction in the trace and issues it to the
     * LLC.
     * */
    void step(bool warmup=false);
private:
    void l1_access(SimpleCache&, uint64_t, bool write);
    void l2_access(uint64_t, bool write);
    void l3_access(uint64_t, bool write);

    void direct_fill(uint64_t, SimpleCache&);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // SIMPLE_CORE_DRIVER_h
