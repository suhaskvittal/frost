/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "core.h"
#include "os.h"
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
{
    // Initialize caches.
    L2_ = l2_ptr(new L2Cache(GL_LLC));
    L1I_ = l1i_ptr(new L1ICache(L2_));
    L1D_ = l1d_ptr(new L1DCache(L2_));

    ftb_.reserve(CORE_FTB_SIZE);
}

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

    std::string header = "CORE_" + std::to_string(static_cast<int>(coreid_));

    print_stat(stats_stream_, header, "INST", finished_inst_num_);
    print_stat(stats_stream_, header, "CYCLES", GL_CYCLE);
    print_stat(stats_stream_, header, "IPC", ipc);

    print_stat(stats_stream_, header, "IFMEM_STALLS", s_ifmem_stalls_);
    print_stat(stats_stream_, header, "DISP_STALLS", s_disp_stalls_);

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

    inst->ready_for_icache_access = true;
    inst->pip = GL_OS->translate_byteaddr(inst->ip);
    
    la.inst = std::move(inst);
    la.valid = true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::ifmem(size_t fwid)
{
    Latch& la = la_iftr_ifmem_[fwid];
    la.stalled = false;
    if (!la.valid)
        return;
    Transaction t(coreid_, la.inst.get(), TransactionType::READ, LINEADDR(la.inst->pip), true);
    // Cannot proceed if the ip has not been translated or if
    // we cannot submit `t`.
    if (!la.inst->ready_for_icache_access 
            || ftb_.size() == CORE_FTB_SIZE
            || !L1I_->io_->add_incoming(t)) 
    {
        la.stalled = true;
        ++s_ifmem_stalls_;
        return;
    }
    la.valid = false;
    // Place into the fetch target buffer.
    ftb_.push_back(std::move(la.inst));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
move_byteaddr_to_lineaddr(const std::vector<uint64_t>& orig, std::unordered_set<uint64_t>& s)
{
    std::transform(orig.begin(), orig.end(), std::inserter(s, s.begin()),
            [] (uint64_t byteaddr)
            {
                return LINEADDR(byteaddr);
            });
}

void
Core::disp(size_t fwid)
{
    auto inst_it = std::find_if(ftb_.begin(), ftb_.end(),
                            [] (const iptr_t& inst)
                            {
                                return inst->inst_load_done;
                            });
    if (inst_it == ftb_.end()) {
        ++s_disp_stalls_;
        return;
    }
    auto& inst = *inst_it;
    // Otherwise, install into the ROB.
    inst->cycle_issued = GL_CYCLE;
    // Check if `la.inst` is a memory instruction. 
    // If it is, then update its metadata for cache accesses.
    // If not, then set its completion cycle to next cycle.
    if (inst->is_mem_inst()) {
        move_byteaddr_to_lineaddr(inst->loads, inst->v_ld_lineaddr);
        move_byteaddr_to_lineaddr(inst->stores, inst->v_st_lineaddr);
    } else {
        inst->cycle_done = GL_CYCLE+1;
    }
    rob_.push_back(std::move(inst));
    ftb_.erase(inst_it);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
translate_and_delete(std::unordered_set<uint64_t>& v, std::unordered_set<uint64_t>& p)
{
    for (auto it = v.begin(); it != v.end(); ) {
        // TODO: add TLB logic. Right now, since we assume perfect translation,
        // we will always erase `it`.
        p.insert(GL_OS->translate_lineaddr(*it));
        it = v.erase(it);
    }
}

template <class CACHE_TYPE, class PRED> inline void
submit_and_delete(std::unique_ptr<CACHE_TYPE>& c, std::unordered_set<uint64_t>& p, PRED pred)
{
    for (auto it = p.begin(); it != p.end(); ) {
        if (pred(*it))
            it = p.erase(it);
        else
            ++it;
    }
}

void
Core::operate_rob()
{
    // First, check if we can complete any ROB entries.
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++) {
        if (rob_.empty() || GL_CYCLE < rob_.front()->cycle_done)
            break;
        rob_.pop_front();
        ++finished_inst_num_;
    }
    // Nothing to be done if the rob is empty
    if (rob_.empty())
        return;
    // Check if any ROB entries can access the L1D$ or
    // need address translation.
    for (iptr_t& inst : rob_) {
        submit_and_delete(L1D_, inst->p_ld_lineaddr,
                [this, i_p = inst.get()] (uint64_t lineaddr)
                {
                    Transaction t(this->coreid_, i_p, TransactionType::READ, lineaddr);
                    if (L1D_->io_->add_incoming(t)) {
                        ++i_p->loads_in_progress;
                        return true;
                    } else {
                        return false;
                    }
                });
        submit_and_delete(L1D_, inst->p_st_lineaddr,
                [this, i_p = inst.get()] (uint64_t lineaddr)
                {
                    Transaction t(this->coreid_, i_p, TransactionType::WRITE, lineaddr);
                    if (L1D_->io_->add_incoming(t))
                        return true;
                    else
                        return false;
                });

        translate_and_delete(inst->v_ld_lineaddr, inst->p_ld_lineaddr);
        translate_and_delete(inst->v_st_lineaddr, inst->p_st_lineaddr);
        // If this is a store instruction and it has launched all its stores, finish now 
        if (inst->mem_inst_is_done() && inst->loads.empty())
            inst->cycle_done = GL_CYCLE;
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
                t.inst->inst_load_done = true;
            });
    // Handle L1D outgoing queue
    drain_cache_outgoing_queue(L1D_,
            [] (const Transaction& t)
            {
                --t.inst->loads_in_progress;
                if (t.inst->mem_inst_is_done())
                    t.inst->cycle_done = GL_CYCLE;
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
                auto& core = GL_CORES[t.coreid];
                core->L2_->mark_load_as_done(t.address);
            });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
