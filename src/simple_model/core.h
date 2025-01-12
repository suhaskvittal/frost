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
    using rob_type = std::deque<inst_ptr>;
    using tracereader_type = TraceReader<MemsimTraceFormat>;

    rob_type rob_;
    size_t rob_size_ =0;
    inst_ptr asleep_inst_ =nullptr;

    std::string      trace_file_;
    tracereader_type trace_reader_;
    inst_ptr         next_mem_inst_ =nullptr;

    uint64_t curr_inst_num_ =0;
    uint64_t inst_warmup_ =0;

    std::stringstream stats_stream_;
public:
    Core(uint8_t coreid, std::string trace_file);

    void tick_warmup(void);
    void tick(void);

    void checkpoint_stats(void);
    void print_stats(std::ostream&);
private:
    void ifetch(void);
    void operate_rob(void);

    bool do_llc_access(inst_ptr);

    inst_ptr next_inst(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void drain_llc_outgoing_queue(void);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // SIMPLE_MODEL_CORE_h
