/*
 *  author: Suhas Vittal
 *  date:   11 December 2024
 * */

#include "constants.h"
#include "globals.h"
#include "memsys.h"

#include "simple_model/core.h"
#include "simple_model/os.h"
#include "transaction.h"
#include "util/stats.h"
#include "util/stats/cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

Core::Core(uint8_t coreid, std::string trace_file)
    :coreid_(coreid),
    trace_file_(trace_file),
    trace_reader_(trace_file)
{}

Core::~Core(void) {}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick_warmup()
{
    iptr_t inst = next_inst();
    ++inst_warmup_;

    if (inst != nullptr) {
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
Core::checkpoint_stats()
{
    double ipc = mean(finished_inst_num_, GL_CYCLE);
    std::string header = "CORE_" + std::to_string(static_cast<int>(coreid_));

    stats_stream_ << BAR << "\n";

    print_stat(stats_stream_, header, "INST", finished_inst_num_);
    print_stat(stats_stream_, header, "CYCLES", GL_CYCLE);
    print_stat(stats_stream_, header, "IPC", ipc);

    stats_stream_ << BAR << "\n";

    print_cache_stats_for_core_header(stats_stream_);
    print_cache_stats_for_core(this, GL_LLC, stats_stream_, header + "_LLC");
}

void
Core::print_stats(std::ostream& out)
{
    out << stats_stream_.str();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::ifetch()
{
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++) {
        if (rob_size_ == CORE_ROB_SIZE)
            return;
        iptr_t inst = next_inst();
        if (inst != nullptr) {
            do_llc_access(inst);
            // Install inst into the ROB.
            rob_.push_back(inst);
            ++rob_size_;
        } else if (rob_.empty()) {
            ++finished_inst_num_;
        } else {
            ++rob_.back()->rob_refs;
            ++rob_size_;
        }
        ++curr_inst_num_;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::operate_rob()
{
    for (size_t i = 0; i < CORE_FETCH_WIDTH && !rob_.empty(); ) {
        iptr_t& inst = rob_.front();
        if (GL_CYCLE < inst->cycle_done)
            break;
        inst->retired = true;

        size_t rob_ref_updates = std::min(CORE_FETCH_WIDTH-i, inst->rob_refs);

        inst->rob_refs -= rob_ref_updates;
        rob_size_ -= rob_ref_updates;
        finished_inst_num_ += rob_ref_updates;

        if (inst->rob_refs == 0)
            rob_.pop_front();

        i += rob_ref_updates;
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
                        if (GL_LLC->io_->add_incoming(trans))
                            x.state = AccessState::IN_CACHE;
                    }
                }
            });
}

void
Core::do_llc_access(iptr_t& inst)
{
    do_ldst<TransactionType::READ>(inst, coreid_);
    do_ldst<TransactionType::WRITE>(inst, coreid_);

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
        next_mem_inst_ = iptr_t(new Instruction(trace_reader_()));
    }
    if (next_mem_inst_->inst_num <= curr_inst_num_ + inst_warmup_ ) {
        iptr_t out = std::move(next_mem_inst_);
        next_mem_inst_ = nullptr;
        // Translate all addresses now.
        for (Memop& x : out->loads)
            x.p_lineaddr = GL_OS->translate_lineaddr(x.v_lineaddr, coreid_);
        for (Memop& x : out->stores)
            x.p_lineaddr = GL_OS->translate_lineaddr(x.v_lineaddr, coreid_);
        return out;
    } else {
        return nullptr;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
drain_llc_outgoing_queue()
{
    drain_cache_outgoing_queue(GL_LLC,
            [] (const Transaction& t)
            {
                for (auto& inst : t.inst_list) {
                    ++inst->num_loads_in_state[static_cast<int>(AccessState::DONE)];
                    if (inst->is_done())
                        inst->cycle_done = GL_CYCLE;
                }
            });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
