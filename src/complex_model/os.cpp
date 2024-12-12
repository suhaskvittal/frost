/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "globals.h"
#include "memsys.h"

#include "complex_model/core.h"
#include "complex_model/os.h"
#include "complex_model/os/ptw.h"
#include "complex_model/os/vmem.h"
#include "transaction.h"
#include "os/address.h"
#include "util/stats.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

OS::OS(ptwc_init_list_t ptwc_init)
{
    for (size_t i = 0; i < NUM_THREADS; i++) {
        // Initialize virtual memory and PTWs
        vmem_[i] = vmem_ptr(new VirtualMemory(free_list_.get_and_reserve_free_page_frame()));
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

void
OS::print_stats(std::ostream& out)
{
    out << BAR << "\n";
    print_stat(out, "OS", "PAGE_FAULTS", free_list_.s_page_faults_);
    out << BAR << "\n";
    // Print out stats of page table walker caches.
    out << std::setw(12) << std::left << "PTW$";
    for (size_t i = 0; i < PT_LEVELS-1; i++) {
        std::string stat = "MISS_RATE_L" + std::to_string(i+1);
        out << std::setw(16) << std::left << stat;
    }
    out << std::setw(16) << "TLB MISSES"
        << std::setw(16) << "L1D$ ACCESSES"
        << "\n" << BAR;
    for (size_t i = 0; i < NUM_THREADS; i++) {
        std::string name = "CORE_" + std::to_string(i);
        out << "\n" << std::setw(12) << name;
        for (size_t j = 0; j < PT_LEVELS-1; j++) {
            auto& c = ptw_[i]->caches_[j];
            double miss_rate = mean(c->s_misses_, c->s_accesses_);
            out << std::setw(16) << std::setprecision(3) << miss_rate;
        }
        out << std::setw(16) << ptw_[i]->s_tlb_misses_
            << std::setw(16) << ptw_[i]->s_requests_to_data_cache_;
    }
    out << "\n" << BAR << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
