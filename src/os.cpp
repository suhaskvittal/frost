/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "globals.h"
#include "memsys.h"

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

OS::OS(ptwc_init_list_t ptwc_init)
{
    for (size_t i = 0; i < NUM_THREADS; i++) {
        // Initialize virtual memory and PTWs
        core_mmu_[i].vmem = vmem_ptr(new VirtualMemory(get_and_reserve_free_page_frame()));
        core_mmu_[i].ptw = ptw_ptr(new PageTableWalker(
                                static_cast<uint8_t>(i),
                                L2TLB_[i],
                                GL_CORES[i]->L1D_,
                                core_mmu_[i].vmem,
                                ptwc_init));
        // Now do TLBs.
        L2TLB_[i] = l2tlb_ptr(new L2TLB("L2TLB", core_mmu_[i].ptw));
        ITLB_[i] = itlb_ptr(new ITLB("iTLB", L2TLB_[i]));
        DTLB_[i] = dtlb_ptr(new DTLB("dTLB", L2TLB_[i]));
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
OS::warmup_translate(uint64_t byteaddr, uint8_t coreid, bool is_inst)
{
    auto [vpn, offset] = split_address<1>(byteaddr);
    if (is_inst)
        ITLB_[coreid]->warmup_access(vpn, false);
    else
        DTLB_[coreid]->warmup_access(vpn, false);
    uint64_t pfn = core_mmu_[coreid].vmem->get_pfn(vpn);
    return join_address<1>(pfn, offset);
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
        auto [vpn, off] = split_address<LINESIZE>(*it);
        if (vpn == match_vpn) {
            to.insert(join_address<LINESIZE>(vmem->get_pfn(vpn), off));
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
        core_mmu_[i].ptw->tick();
        // Handle TLB outgoing requests.
        drain_cache_outgoing_queue(L2TLB_[i],
                [this, i] (const Transaction& t)
                {
                    if (t.address_is_ip)
                        this->ITLB_[i]->mark_load_as_done(t.address);
                    else
                        this->DTLB_[i]->mark_load_as_done(t.address);
                });
        drain_cache_outgoing_queue(ITLB_[i],
                [this, i] (const Transaction& t)
                {
                    // Just need to update instruction.
                    t.inst->ready_for_icache_access = true;
                    t.inst->pip = translate<1>(t.inst->ip, this->core_mmu_[i].vmem);
                });
        drain_cache_outgoing_queue(DTLB_[i],
                [this, i] (const Transaction& t)
                {
                    auto& inst = t.inst;
                    uint64_t vpn = t.address;
                    move_on_translate(inst->v_ld_lineaddr, inst->p_ld_lineaddr, vpn, this->core_mmu_[i].vmem);
                    move_on_translate(inst->v_st_lineaddr, inst->p_st_lineaddr, vpn, this->core_mmu_[i].vmem);
                });
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
OS::translate_ip(uint8_t coreid, iptr_t inst)
{
    uint64_t vpn = inst->ip >> numeric_traits<PAGESIZE>::log2;
    // Attempt to access iTLB.
    Transaction t(coreid, inst, TransactionType::TRANSLATION, vpn, true);
    return ITLB_[coreid]->io_->add_incoming(t);
}

bool
OS::translate_ldst(uint8_t coreid, iptr_t inst, uint64_t addr)
{
    uint64_t vpn = addr >> numeric_traits<PAGESIZE/LINESIZE>::log2;
    Transaction t(coreid, inst, TransactionType::TRANSLATION, vpn, false);
    return DTLB_[coreid]->io_->add_incoming(t);
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

void
OS::print_stats(std::ostream& out)
{
    out << BAR << "\n";
    print_stat(out, "OS", "PAGE_FAULTS", s_page_faults_);
    out << BAR << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
