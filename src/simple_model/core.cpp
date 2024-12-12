/*
 *  author: Suhas Vittal
 *  date:   11 December 2024
 * */

#include "simple_model/core.h"
#include "simple_model/os.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

Core::Core(uint8_t coreid, std::string trace_file)
    :coreid(coreid),
    trace_file(trace_file),
    trace_reader_(new TraceReader(trace_file))
{}

Core::~Core(void) {}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick_warmup()
{
    iptr_t inst = next_inst();
    ++inst_warmup_;

    if (inst->is_mem_inst()) {
        for (Memop& x : inst->loads)
            GL_LLC->warmup_access(x.p_lineaddr, false);
        for (Memop& x : inst->stores)
            GL_LLC->warmup_access(x.p_lineaddr, true);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick()
{
    operate_rob();
    ifetch();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::ifetch()
{
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++) {
        if (rob_.size() == CORE_ROB_SIZE)
            return;
        iptr_t inst = next_inst();
        if (inst->is_mem_inst())
            do_llc_access(inst);
        else
            inst->cycle_done = GL_CYCLE+1;
        // Install inst into the ROB.
        rob_.push_back(std::move(inst));
        ++curr_inst_num_;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::operate_rob()
{
    for (size_t i = 0; i < CORE_FETCH_WIDTH && !rob_.empty(); i++) {
        iptr_t& inst = rob_.front();
        if (GL_CYCLE < inst->cycle_done)
            break;
        inst->retired = true;
        rob_.pop_front();
        ++finished_inst_num_;
    }
    if (rob_.empty())
        return;
    // Check if any entries failed to access the LLC in ifetch.
    for (iptr_t& inst : rob_) {
        if (GL_CYCLE >= inst->cycle_done)
            continue;
        do_llc_access(inst);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <TransactionType T> void
do_ldst(iptr_t& inst, uint8_t coreid)
{
    auto& st = (T == TransactionType::READ) ? inst->num_loads_in_state : inst->num_stores_in_state;
    auto& v = (T == TransactionType::READ) ? inst->loads : inst->stores;

    inst_do_func_dependent_on_state<AccessState::IN_CACHE>(st, v,
            [inst, coreid] (Instruction::memop_list_t& v)
            {
                for (Memop& x : v) {
                    if (x.state == AccessState::NOT_READY) {
                        Transaction trans(coreid, inst, T, x.p_lineaddr);
                        if (c->io_->add_incoming(trans))
                            x.state = AccessState::IN_CACHE;
                    }
                }
            });
}

void
Core::do_llc_access(iptr_t& inst)
{
    do_ldst<TransactionType::READ>(inst, coreid_);
    do_ldst<TransactionType::WRITES>(inst, coreid_);

    if (inst->loads.empty() && inst->is_done())
        inst->cycle_done = GL_CYCLE+1;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

iptr_t
Core::next_inst()
{
    if (next_mem_inst_ == nullptr) {
        // Fetch from trace reader.
        next_mem_inst_ = iptr_t(new Instruction(trace_reader_->read()));
    }
    iptr_t out; 
    if (next_mem_inst_->inst_num - inst_warmup_ <= curr_inst_num_)
        out = std::move(next_mem_inst_);
    else
        out = iptr_t(new Instruction(curr_inst_num_));
    // Translate all addresses now.
    for (Memop& x : inst->loads)
        x.p_lineaddr = GL_OS->translate_lineaddr(x.v_lineaddr, coreid_);
    for (Memop& x : inst->stores)
        x.p_lineaddr = GL_OS->translate_lineaddr(x.v_lineaddr, coreid_);
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
