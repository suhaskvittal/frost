/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "core.h"
#include "util/stats.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline uint64_t LINEADDR(uint64_t byteaddr)
{
    return byteaddr >> numeric_traits<LINESIZE>::log2;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

Core::Core(uint8_t coreid, std::string trace_file)
    :coreid_(coreid),
    trace_reader_(trace_file)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick()
{
    operate_caches();
    operate_rob();
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++) {
        disp(i);
        ifmem(i);
        iftr(i);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
 
template <class CACHE> inline void
print_cache_stats_for_core(Core* c, const std::unique_ptr<CACHE>& cache, std::ostream& out, std::string_view header)
{
    uint8_t id = c->coreid_;
    uint64_t inst = c->finished_inst_num_;
    
    uint64_t accesses = cache->s_accesses_.at(id),
             misses = cache->s_misses_.at(id),
             invalidates = cache->s_invalidates_.at(id),
             write_alloc = cache->s_write_alloc_.at(id);

    double apki = mean(accesses, inst) * 1000.0,
           mpki = mean(misses, inst) * 1000.0;
    double miss_penalty = mean(cache->s_tot_penalty_.at(id), cache->s_num_penalty_.at(id));

    print_stat(out, header, "ACCESSES", accesses);
    print_stat(out, header, "MISSES", misses);
    if (invalidates > 0)
        print_stat(out, header, "INVALIDATES", invalidates);
    if (write_alloc > 0)
        print_stat(out, header, "WRITE_ALLOCATES", write_alloc);
    print_stat(out, header, "APKI", apki);
    print_stat(out, header, "MPKI", mpki);
    print_stat(out, header, "MISS_PENALTY", miss_penalty);
}

void
Core::checkpoint_stats()
{
    double ipc = mean(finished_inst_num_, GL_CYCLE);

    std::string header = "CORE_" + std::to_string(static_cast<int>(coreid));

    print_stat(stats_stream_, header, "INST", finished_inst_num_);
    print_stat(stats_stream_, header, "CYCLES", GL_CYCLE);
    print_stat(stats_stream_, header, "IPC", ipc);

    print_cache_stats_for_core(this, L1I_, stats_stream_, header + "_L1I$");
    print_cache_stats_for_core(this, L1D_, stats_stream_, header + "_L1D$");
    print_cache_stats_for_core(this, L2_, stats_stream_, header + "_L2$");
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
Core::iftr(size_t fwid)
{
    Latch& la = la_iftr_ifmem_[fwid];
    if (la.stalled)
        return;
    iptr_t inst = iptr_t(new Instruction(trace_reader_.read())); 
    inst->cycle_fetched = GL_CYCLE;

    inst->ip_translated = true;
    inst->pip = GL_OS->translate_byteaddr(inst->ip);
    
    la.inst = std::move(inst);
    la.valid = true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::ifmem(size_t fwid)
{
    Latch& la = la_iftr_ifmem_[fwid],
         & la_next = la_ifmem_disp_[fwid];
    if (!la.valid || la_next.stalled)
        return;
    Transaction t(coreid_, nullptr, TransactionType::READ, LINEADDR(la.inst->pip), true);
    // Cannot proceed if the ip has not been translated or if
    // we cannot submit `t`.
    if (!la.inst->ip_translated || !L1I_->io_->add_incoming(t)) {
        la.stalled = true;
        return;
    }
    la.valid = false;
    la.stalled = false;
    // Update next latch.
    la_next.inst = std::move(la.inst);
    la_next.valid = true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::disp(size_t fwid)
{
    Latch& la = la_ifmem_disp_[fwid];    
    if (!la.valid)
        return;
    // Install the instruction into the ROB
    if (!la.inst->data_avail || rob.size() == CORE_ROB_SIZE) {
        la.stalled = true;
        return;
    }
    la.inst->cycle_issued = GL_CYCLE;
    // Check if `la.inst` is a memory instruction. If not, then mark its completion
    // cycle as the next cycle.
    if (!la.inst->is_mem_inst())
        la.inst->cycle_done = GL_CYCLE+1;
    rob_.push_back(std::move(la.inst));
    la.valid = false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool
ready_for_dcache(const iptr_t& inst)
{
    return inst->is_mem_inst()
            && inst->p_ld_lineaddr.size() == inst->loads.size()
            && inst->p_st_lineaddr.size() == inst->stores.size()
            && !inst->awaiting_loads;
}

inline bool
need_translation(const iptr& inst)
{
    return inst->is_mem_inst()
            && (inst->p_ld_lineaddr.size() < inst->loads.size() 
                    || inst->p_st_lineaddr.size() < inst->stores.size())
            && !inst->awaiting_loads;
}

void
Core::operate_rob()
{
    // First, check if we can complete any ROB entries.
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++) {
        if (rob_.empty() == 0 || GL_CYCLE < rob_.front()->cycle_done)
            break;
        rob_.pop_front();
    }
    // Nothing to be done if the rob is empty
    if (rob_.empty()
        return;
    // Check if any ROB entries can access the L1D$ or
    // need address translation.
    for (iptr_t& inst : rob_) {
        if (ready_for_dcache(inst)) {
            for (uint64_t addr : inst->p_ld_lineaddr) {
                Transaction t(coreid_, inst.get(), TransactionType::READ, addr);
                L1D_->io_->add_incoming(t);
            }

            for (uint64_t addr : inst->p_st_lineaddr) {
                Transaction t(coreid_, inst.get(), TransactionType::WRITE, addr);
                L1D_->io_->add_incoming(t);
            }
            inst->awaiting_loads = true;
        }

        if (needs_translation(inst)) {
            for (uint64_t addr : inst->loads)
                inst->p_ld_lineaddr.insert(GL_OS->translate_lineaddr(LINEADDR(addr)));
            for (uint64_t addr : inst->stores)
                inst->p_st_lineaddr.insert(GL_OS->translate_lineaddr(LINEADDR(addr)));
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::operate_caches()
{
    // First, drain L2 outgoing queue.
    drain_cache_outgoing_queue(L2_, 
            [this] (const Transaction& t)
            {
                if (t.address_is_ip)
                    this->L1I_->mark_load_as_done(t.address);
                else
                    this->L1D_->mark_load_as_done(t.address);
            });
    // Handle L1I outgoing queue
    drain_cache_outgoing_queue(L1I_,
            [this] (const Transaction& t)
            {
                auto la_it = std::find_if(this->la_ifmem_disp_.begin(), this->la_ifmem_disp_.end(),
                                        [t] (const Latch& la)
                                        {
                                            return LINEADDR(la.inst->pip) == t.address;
                                        });
                if (la_it == this->la_ifmem_disp_.end()) {
                    std::cerr << "core: zombie instruction wakeup: phys. IP = " << t.address << "\n";
                    exit(1);
                }
                // Otherwise mark that the instruction is now available.
                la_it->inst->inst_data_avail = true;
            });
    // Handle L1D outgoing queue
    drain_cache_outgoing_queue(L1D_,
            [] (const Transaction& t)
            {
                if (trans_is_read(t.type))
                    t.inst->p_ld_lineaddr.erase(t.address);
                else
                    t.inst->p_st_lineaddr.erase(t.address);
            });
    // Tick each of the caches.
    L2_->tick();
    L1I_->tick();
    L1D_->tick();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
drain_llc_outgoing_queue()
{
    drain_cache_outgoing_queue(GL_LLC,
            [] (const Transaction& t)
            {
                auto core = GL_CORES[t.coreid];
                core->L2_->mark_load_as_done(t.address);
            });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
