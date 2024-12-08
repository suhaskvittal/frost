/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "core.h"
#include "os.h"
#include "transaction.h"
#include "util/stats.h"

#include <iostream>

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
OS::tick()
{
    // Handle TLB outgoing requests.
    drain_cache_outgoing_queue(ITLB_,
            [this] (const Transaction& t)
            {
                pending_ip_translations[inst] = TranslationState::DONE;
            });
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

    uint64_t vpn = inst->ip >> numeric_traits<PAGESIZE>::log2,
             off = fast_mod<PAGESIZE>(inst->ip);
    // Attempt to access iTLB.
    Transaction t(coreid, inst, TransactionType::TRANSLATION, vpn, true);
    if (ITLB_->add_incoming(t)) {
        pending_ip_translations[inst] = TranslationState::IN_TLB;
        return true;
    } else {
        return false;
    }
}

bool
OS::check_if_ip_translation_is_done(iptr_t inst)
{
    auto it = pending_ip_translations_.find(inst);
    if (it->second == TranslationState::DONE) {
        pending_ip_translations_.erase(it);
        return true;
    } else {
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
OS::translate_byteaddr(uint64_t addr)
{
    return translate_addr<PAGESIZE>(addr);
}

uint64_t
OS::translate_lineaddr(uint64_t addr)
{
    return translate_addr<PAGESIZE/LINESIZE>(addr);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
OS::get_pfn(uint64_t vpn)
{
    // Do page walk.
    if (!pt_.count(vpn))
        handle_page_fault(vpn);
    return pt_[vpn];
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
OS::handle_page_fault(uint64_t vpn)
{
    ++s_page_faults_;
    for (size_t i = 0; i < 1024; i++) {
        size_t pfn = fast_mod<NUM_PAGE_FRAMES>(rng());
        size_t ii = pfn >> 6,
               jj = pfn & 0x3f;
        bool is_taken = free_page_frames_[ii] & (1L << jj);
        if (!is_taken) {
            free_page_frames_[ii] |= (1L << jj);
            pt_[vpn] = pfn;
            return;
        }
    }
    size_t num_free = std::accumulate(free_page_frames_.begin(),
                                        free_page_frames_.end(),
                                        static_cast<size_t>(0),
                                        [] (uint64_t x, uint64_t y)
                                        {
                                            return x + __builtin_popcount(~y);
                                        });
    std::cerr << "os: failed to find free page frame, free page frames: " << num_free << "\n";
    exit(1);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
