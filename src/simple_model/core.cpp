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

inline Transaction init_trans_from_inst(inst_ptr inst, uint8_t coreid)
{
    Transaction::Type trans_type = inst->is_store ? Transaction::Type::WRITE : Transaction::Type::READ;
    uint64_t ip = 0;
#if defined(TRACE_FORMAT_IMAT)
    ip = inst->ip;
    if ((ip >> ilog2(LINESIZE)) == inst->v_lineaddr)
        trans_type = Transaction::Type::INSTRUCTION;
#endif

    return Transaction{coreid, ip, inst->p_lineaddr, inst, trans_type};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

Core::Core(uint8_t coreid, std::string trace_file)
    :coreid_(coreid),
    trace_file_(trace_file),
    trace_reader_(trace_file)
{
    next_mem_inst_ = new Instruction(trace_reader_());
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick_warmup()
{
    inst_ptr inst = next_inst();
    ++inst_warmup_;

    if (inst != nullptr)
    {
        GL_LLC->warmup_access(init_trans_from_inst(inst, coreid_));
        delete inst;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick()
{
    operate_rob();
    
    if (asleep_inst_ != nullptr)
    {
        if (do_llc_access(asleep_inst_))
        {
            rob_.push_back(asleep_inst_);
            ++rob_size_;
            ++curr_inst_num_;
            asleep_inst_ = nullptr;
        }
        else
            return;
    }

    ifetch();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::checkpoint_stats()
{
    double ipc = mean(static_cast<uint64_t>(finished_inst_num_), GL_CYCLE - GL_CYCLE_WARMUP);
    std::string header = "CORE_" + std::to_string(static_cast<int>(coreid_));

    stats_stream_ << BAR << "\n";

    print_stat(stats_stream_, header, "TRACE", trace_file_);
    print_stat(stats_stream_, header, "INST", finished_inst_num_);
    print_stat(stats_stream_, header, "CYCLES", GL_CYCLE - GL_CYCLE_WARMUP);
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
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++)
    {
        if (rob_size_ == CORE_ROB_SIZE)
            return;
        inst_ptr inst = next_inst();
        // Install inst into the ROB.
        if (inst != nullptr)
        {
            if (!do_llc_access(inst))
            {
                asleep_inst_ = inst;
                return;
            }
            rob_.push_back(inst);
            ++rob_size_;

            // If this is the first ROB entry, then set `rob_stall_start_cycle_` to `GL_CYCLE`.
            if (rob_.size() == 1)
                rob_stall_start_cycle_ = GL_CYCLE;
        }
        else if (rob_.empty())
        {
            ++finished_inst_num_;
        }
        else
        {
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
    for (size_t i = 0; i < CORE_FETCH_WIDTH && !rob_.empty(); )
    {
        inst_ptr inst = rob_.front();
        if (GL_CYCLE < inst->cycle_done)
        {
            if (GL_CYCLE - rob_stall_start_cycle_ > 10'000'000)
            {
                // Simulator is deadlocked:
                std::cerr << "\nCore " << (coreid_+0) << " deadlock in cycle " << GL_CYCLE << " detected:\n";
                std::cerr << "Instruction is " << (inst->is_store ? "store" : "load") 
                    << " to " << inst->p_lineaddr << "\n";
                GL_LLC->deadlock_find_inst(inst);
                GL_DRAM->deadlock_find_inst(inst);
                exit(1);
            }
            break;
        }
        size_t rob_ref_updates = std::min(CORE_FETCH_WIDTH-i, inst->rob_refs);

        inst->rob_refs -= rob_ref_updates;
        rob_size_ -= rob_ref_updates;
        finished_inst_num_ += rob_ref_updates;
        if (inst->rob_refs == 0)
        {
            rob_.pop_front();
            delete inst;
        }

        i += rob_ref_updates;
        rob_stall_start_cycle_ = GL_CYCLE;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
Core::do_llc_access(inst_ptr inst)
{
    Transaction trans = init_trans_from_inst(inst, coreid_);
    bool success = GL_LLC->can_accept(trans) && GL_LLC->add_incoming(trans);
    if (inst->is_store && success)
        inst->cycle_done = GL_CYCLE + 1;
    return success;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inst_ptr
Core::next_inst()
{
    // Fetch from trace reader.
    if (next_mem_inst_->inst_num <= curr_inst_num_ + inst_warmup_ )
    {
        inst_ptr out = next_mem_inst_;
        next_mem_inst_ = new Instruction(trace_reader_());
        // Translate all addresses now.
        out->p_lineaddr = GL_OS->translate_lineaddr(out->v_lineaddr, coreid_);
        return out;
    } 
    else
        return nullptr;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
drain_llc_outgoing_queue()
{
    drain_cache_outgoing_queue(GL_LLC,
            [] (const Transaction& t)
            {
                t.inst->cycle_done = GL_CYCLE;
                t.inst->state = AccessState::DONE;
            });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
