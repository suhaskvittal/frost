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
        vmem_[i] = vmem_ptr(new VirtualMemory(get_and_reserve_free_page_frame()));
        ptw_[i] = ptw_ptr(new PageTableWalker(
                                static_cast<uint8_t>(i),
                                L2TLB_[i],
                                GL_CORES[i]->L1D_,
                                vmem_[i],
                                ptwc_init));
        // Now do TLBs.
        L2TLB_[i] = l2tlb_ptr(new L2TLB("L2TLB", ptw_[i]));
        ITLB_[i] = itlb_ptr(new ITLB("iTLB", L2TLB_[i]));
        DTLB_[i] = dtlb_ptr(new DTLB("dTLB", L2TLB_[i]));
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t N, class CACHE_TYPE> uint64_t
warmup_translate(std::unique_ptr<CACHE_TYPE>& tlb, uint64_t addr, std::unique_ptr<VirtualMemory>& vmem)
{
    auto [vpn, offset] = split_address<N>(addr);
    tlb->warmup_access(vpn, false);
    uint64_t pfn = vmem->get_pfn(vpn);
    return join_address<N>(pfn, offset);
}

uint64_t
OS::warmup_translate_ip(uint8_t coreid, uint64_t ip)
{
    return warmup_translate<1>(ITLB_[coreid], ip, vmem_[coreid]);
}

uint64_t
OS::warmup_translate_ldst(uint8_t coreid, uint64_t addr)
{
    return warmup_translate<LINESIZE>(DTLB_[coreid], addr, vmem_[coreid]);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
OS::tick()
{
    // Tick all PTWs
    for (size_t i = 0; i < NUM_THREADS; i++) {
        ptw_[i]->tick();
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
                    for (auto inst : t.inst_list) {
                        inst->ip_state = AccessState::READY;
                        inst->pip = translate<1>(inst->ip, this->vmem_[i]);
                    }
                });
        drain_cache_outgoing_queue(DTLB_[i],
                [this, i] (const Transaction& t)
                {
                    uint64_t vpn = t.address;
                    uint64_t pfn = this->vmem_[i]->get_pfn(vpn);
                    for (auto inst : t.inst_list)
                        inst_dtlb_done(inst, vpn, pfn);
                });
        L2TLB_[i]->tick();
        ITLB_[i]->tick();
        DTLB_[i]->tick();
        ptw_[i]->tick();
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
    for (size_t i = 0; i < 2048; i++) {
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
    std::cerr << "os: failed to find free page frame, free page frames: " << num_free
        << ", total = " << NUM_PAGE_FRAMES << "\n";
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
