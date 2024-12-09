/*
 *  author: Suhas Vittal
 *  date:   7 December 2024
 * */

#include "memsys.h"
#include "os/ptw.h"
#include "os/ptw/cache.h"
#include "os/vmem.h"
#include "transaction.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

PageTableWalker::IO::IO(PageTableWalker* ptw)
    :owner(ptw)
{}

bool
PageTableWalker::IO::add_incoming(Transaction t)
{
    owner->handle_tlb_miss(t);
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

PageTableWalker::PageTableWalker(
        uint8_t coreid,
        l2tlb_ptr& l2tlb,
        l1d_ptr& l1d,
        vmem_ptr& vmem,
        const ptwc_init_array_t& ptwc_init)
    :coreid_(coreid),
    L2TLB_(l2tlb),
    L1D_(l1d),
    vmem_(vmem)
{
    // Initialize PTW caches.
    for (size_t i = 0; i < PTW_LEVELS; i++) {
        const auto& [sets, ways] = ptwc_init.at(i);
        caches_[i] = ptwc_ptr(new PTWCache(sets, ways));
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
PageTableWalker::tick()
{
    // Handle any accesses that still need to access the L1D$.
    auto ptw_it = std::find_if(ongoing_walks_.begin(), ongoing_walks_.end(),
                        [] (const auto& x)
                        {
                            return x.second.state == PTWState::NEED_ACCESS;
                        });
    if (ptw_it != ongoing_walks_.end()) {
        PTWEntry& e = ptw_it->second;
        Transaction t(coreid_, nullptr, TransactionType::TRANSLATION, e.get_curr_pfn_lineaddr());
        if (L1D_->add_incoming(t))
            e.state = PTWState::WAITING_ON_ACCESS;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
PageTableWalker::handle_tlb_miss(const Transaction& t)
{
    uint64_t vpn = t.address;
    // Initialize PTWEntry for this transaction.
    PTWEntry e;
    e.trans = t;
    e.walk_data = vmem_->do_page_walk(vpn);
    e.curr_level = ptwc_get_initial_directory_level(caches_, vpn);
    ongoing_walks_.insert({vpn, e});
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
PageTableWalker::handle_l1d_outgoing(const Transaction& t)
{
    for (auto it = ongoing_walks_.begin(); it != ongoing_walks_.end(); ) {
        const auto& [vpn, e] = *it;
        if (e.get_curr_pfn_lineaddr() == t.address) {
            if (e.curr_level == 0) {
                // Then we are finished with this walk.
                L2TLB_->mark_load_as_done(vpn);
                it = ongoing_walks_.erase(it);
                continue;
            } else {
                --e.curr_level;
                e.state = PTWState::NEED_ACCESS;
            }
        }
        ++it;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
