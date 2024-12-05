/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "core.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline uint64_t LINEADDR(uint64_t byteaddr)
{
    return byteaddr >> numeric_traits<LINESIZE>::log2;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick()
{
    operate_caches();
    // operate ROB.
    
    // operate frontend stages.
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++) {
        disp(i);
        ifmem(i);
        iftr(i);
    }
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
    Transaction t(coreid_, 0, TransactionType::READ, LINEADDR(la.inst->pip), true);
    // Cannot proceed if the ip has not been translated or if
    // we cannot submit `t`.
    if (!la.inst->ip_translated || !L1I_->add_incoming(t)) {
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
    if (!la.inst->data_avail || rob_size_ == CORE_ROB_SIZE) {
        la.stalled = true;
        return;
    }
    la.inst->cycle_issued = GL_CYCLE;
    rob_[rob_size_++] = std::move(la.inst);
    la.valid = false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::operate_rob()
{
    // First, check if we can complete any ROB entries.
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++) {
        if (rob_size_ == 0 || GL_CYCLE < rob_[rob_head_]->cycle_done)
            break;
        rob_[rob_head_] = nullptr;  // Clear rob entry
        numeric_traits<CORE_ROB_SIZE>::increment_and_mod(rob_head_);
        --rob_size_;
    }
    // Check if any ROB entries can perform L1D$ accesses.
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
                this->rob_[t.robid]->cycle_done = GL_CYCLE;
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
