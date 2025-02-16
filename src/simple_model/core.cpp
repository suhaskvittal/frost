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
        bool is_icache_miss = false;
#if defined(TRACE_FORMAT_IMAT)
        is_icache_miss = (inst->ip >> numeric_traits<LINESIZE>::log2) == inst->v_lineaddr;
#endif

        TransactionType t = inst->is_store ?  TransactionType::READ : TransactionType::WRITE;
        Transaction trans(coreid_, inst, t, inst->p_lineaddr, is_icache_miss);
        GL_LLC->warmup_access(trans);
    }

    delete inst;
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
    double ipc = mean(static_cast<uint64_t>(finished_inst_num_), GL_CYCLE);
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
        }
        else if (rob_.empty())
            ++finished_inst_num_;
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
        inst_ptr& inst = rob_.front();
        if (GL_CYCLE < inst->cycle_done)
        {
            if (GL_CYCLE - rob_stall_start_cycle_ > 1'000'000)
            {
                // Simulator is deadlocked:
                std::cerr << "\nCore " << (coreid_+0) << " deadlock in cycle " << GL_CYCLE << " detected:\n";
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
    TransactionType t = inst->is_store ? TransactionType::WRITE : TransactionType::READ;
    if (GL_LLC->can_accept(0,t))
    {
        bool is_icache_miss = false;
#if defined(TRACE_FORMAT_IMAT)
        is_icache_miss = (inst->ip >> numeric_traits<LINESIZE>::log2) == inst->v_lineaddr;
#endif
        Transaction trans(coreid_, inst, t, inst->p_lineaddr, is_icache_miss);
        GL_LLC->add_incoming(trans);
        inst->state = AccessState::IN_CACHE;

        // We can mark stores as complete early:
        if (inst->is_store)
            inst->cycle_done = GL_CYCLE+1;

        return true;
    }
    else
    {
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inst_ptr
Core::next_inst()
{
    // Fetch from trace reader.
    if (next_mem_inst_->inst_num <= curr_inst_num_ + inst_warmup_ )
    {
        inst_ptr out = std::move(next_mem_inst_);
        next_mem_inst_ = new Instruction(trace_reader_());
        // Translate all addresses now.
        out->p_lineaddr = GL_OS->translate_lineaddr(out->v_lineaddr, coreid_);
        return out;
    } 
    else
    {
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
                for (auto inst : t.inst_list)
                {
                    inst->cycle_done = GL_CYCLE;
                    inst->state = AccessState::DONE;
                }
            });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
