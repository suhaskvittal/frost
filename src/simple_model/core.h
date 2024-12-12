/*
 *  author: Suhas Vittal
 *  date:   11 December 2024
 * */

#ifndef SIMPLE_MODEL_CORE_h
#define SIMPLE_MODEL_CORE_h

#include "instruction.h"
#include "trace/fmt.h"
#include "trace/reader.h"

#include <deque>
#include <sstream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class Core
{
public:
    uint64_t finished_inst_num_ =0;
    bool done_ =false;

    const uint8_t coreid_;
private:
    using rob_t = std::deque<iptr_t>;
    using tracereader_t = TraceReader<MemsimTraceFormat>;

    rob_t rob_;
    size_t rob_size_ =0;

    std::string   trace_file_;
    tracereader_t trace_reader_;
    iptr_t        next_mem_inst_ =nullptr;

    uint64_t curr_inst_num_ =0;
    uint64_t inst_warmup_ =0;

    std::stringstream stats_stream_;
public:
    Core(uint8_t coreid, std::string trace_file);
    ~Core(void);

    void tick_warmup(void);
    void tick(void);

    void checkpoint_stats(void);
    void print_stats(std::ostream&);
private:
    void ifetch(void);
    void operate_rob(void);

    void do_llc_access(iptr_t&);

    iptr_t next_inst(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void drain_llc_outgoing_queue(void);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // SIMPLE_MODEL_CORE_h
