/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "globals.h"
#include "core.h"
#include "os.h"
#include "os/address.h"
#include "os/ptw.h"
#include "os/vmem.h"
#include "transaction.h"
#include "util/stats.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

OS::OS(const ptwc_init_array_t& ptwc_init)
{
    for (size_t i = 0; i < NUM_THREADS; i++) {
        // Initialize virtual memory and PTWs
        core_mmu_[i] = mmu_ptr(new CoreMMU);
        core_mmu_[i]->vmem = vmem_ptr(new VirtualMemory(i));
        core_mmu_[i]->ptw = ptw_ptr(new PageTableWalker(
                                static_cast<uint8_t>(i),
                                L2TLB_,
                                GL_CORE[i]->L1D_,
                                core_mmu_[i]->vmem,
                                ptwc_init));
        // Now do TLBs.
        std::string prefix = "CORE_" + std::to_string(i);
        L2TLB_[i] = l2tlb_ptr(new L2TLB(prefix + "_L2TLB", core_mmu_[i]->ptw));
        ITLB_[i] = itlb_ptr(new ITLB(prefix + "_iTLB", L2TLB_[i]));
        DTLB_[i] = dtlb_ptr(new ITLB(prefix + "_dTLB", L2TLB_[i]));
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
OS::print_stats(std::ostream& out)
{
    out << BAR << "\n";
    print_stat(out, "OS", "PAGE_FAULTS", s_page_faults_);
    out << BAR << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
move_on_translate(
        Instruction::memop_set_t& from,
        Instruction::memop_set_t& to,
        uint64_t match_vpn,
        std::unique_ptr<VirtualMemory>& vmem)
{
    for (auto it = from.begin(); it != from.end(); ) {
        auto& [vpn, off] = split_address<LINESIZE>(*it);
        if (vpn == match_vpn) {
            to.insert(join_address(vmem->get_pfn(vpn), off));
            it = from.erase(it);
        } else {
            ++it;
        }
    }
}

void
OS::tick()
{
    // Tick all PTWs
    for (size_t i = 0; i < NUM_THREADS; i++) {
        core_mmu_[i]->ptw->tick();

        auto& vmem = core_mmu_[i]->vmem;
        // Handle TLB outgoing requests.
        drain_cache_outgoing_queue(L2TLB_[i],
                [itlb = ITLB_[i], dtlb = DTLB_[i]] (const Transaction& t)
                {
                    if (t.address_is_ip)
                        itlb->mark_load_as_done(t.address);
                    else
                        dtlb->mark_load_as_done(t.address);
                });
        drain_cache_outgoing_queue(ITLB_[i],
                [vmem] (Transaction& t)
                {
                    // Just need to update instruction.
                    t.inst->ready_for_cache_access = true;
                    t.inst->pip = translate<1>(t.inst->ip, vmem);
                });
        drain_cache_outgoing_queue(DTLB_[i],
                [vmem] (Transaction& t)
                {
                    auto& inst = t.inst;
                    uint64_t vpn = t.address;
                    move_on_translate(inst->v_ld_lineaddr, inst->p_ld_lineaddr, vpn, vmem);
                    move_on_translate(inst->v_st_lineaddr, inst->p_st_lineaddr, vpn, vmem);
                });
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
OS::translate_ip(uint8_t coreid, iptr_t inst)
{
    if (pending_ip_translations_.count(inst)) {
        std::cerr << "os: instruction #" << inst->inst_num
                << ", ip = " << inst->ip << " from core " << coreid+0
                << " tried to start ip translation but already pending.\n";
        exit(1);
    }

    uint64_t vpn = inst->ip >> numeric_traits<PAGESIZE>::log2;
    // Attempt to access iTLB.
    Transaction t(coreid, inst, TransactionType::TRANSLATION, vpn, true);
    if (ITLB_->add_incoming(t)) {
        return true;
    } else {
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
OS::get_and_reserve_free_page_frame()
{
    ++s_page_faults_;
    for (size_t i = 0; i < 1024; i++) {
        size_t pfn = fast_mod<NUM_PAGE_FRAMES>(rng());
        size_t ii = pfn >> 6,
               jj = pfn & 0x3f;
        bool is_taken = free_page_frames_[ii] & (1L << jj);
        if (!is_taken) {
            free_page_frames_[ii] |= (1L << jj);
            return pfn;
        }
    }
    size_t num_free = std::accumulate(free_page_frames_.begin(), free_page_frames_.end(), 0ull,
                                        [] (uint64_t x, uint64_t y)
                                        {
                                            return x + __builtin_popcount(~y);
                                        });
    std::cerr << "os: failed to find free page frame, free page frames: " << num_free << "\n";
    exit(1);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
