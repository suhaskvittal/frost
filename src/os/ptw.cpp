/*
 *  author: Suhas Vittal
 *  date:   7 December 2024
 * */

#include "memsys.h"
#include "os/ptw.h"
#include "transaction.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

PageTableWalker::IO::IO(PageTableWalker* ptw)
    :owner(ptw)
{}

bool
PageTableWalker::IO::add_incoming(Transaction t)
{
    owner->start_walk(t.address);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

PageTableWalker::PageTableWalker(l2tlb_ptr& l2tlb, l1d_ptr& l1d)
    :L2TLB_(l2tlb),
    L1D_(l1d)
{
    for (size_t i = 0; i < PTW_LEVELS; i++)
        caches_[i] = ptwc_t(PTWC_ENTRIES[i]);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
PageTableWalker::tick()
{
    // Search for entries that still need to access the page table.
    auto ptw_it = std::find_if_not(ongoing_walks_.begin(), ongoing_walks_.end(),
                        [] (const auto& p)
                        {
                            return p.second.state == PTWState::NEED_ACCESS;
                        });
    if (ptw_it != ongoing_walks_.end()) {
        if (L1D_->io_->add_incoming(t))
            e.state = PTWState::WAITING_ON_ACCESS;
    }
    // Search for completed accesses to a page table.
    ptw_it = std::find_if_not(ongoing_walks_.begin(), ongoing_walks_.end(),
                        [] (const auto& p)
                        {
                            return p.second.state == PTWState::ACCESS_DONE;
                        });
    if (ptw_it != ongoing_walks_.end()) {
        // Do next level or complete.
        uint64_t vpn = ptw_it->first;
        PTWEntry& e = ptw_it->second;
        --e.level;
        if (e.level == 0) {
            // Signal L2TLB.
            L2TLB_->mark_load_as_done(vpn);
            ongoing_walks_.erase(ptw_it);
        } else {
            // Iniitate next level access.
            e.pt_address = page_table_address(coreid_, e.level, vpn);
            Transaction t(coreid_, nullptr, TransactionType::TRANSLATION, e.pt_address);
            if (L1D_->io_->add_incoming(t))
                e.state = PTWState::WAITING_ON_ACCESS;
            else
                e.state = PTWState::NEED_ACCESS;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
PageTableWalker::start_walk(uint64_t vpn)
{
    // Do ptwc access
    size_t level = PT_WALK_LEVELS;
    while (level > 0) {
        if (ptwc_access(level-1, vpn)) {
            --level;
        } else {
            break;
        }
    }
    PTWEntry e;
    e.level = level;
    e.state = PTWState::NEED_ACCESS;
    e.pt_address = page_table_address(coreid_, level, vpn);

    ongoing_walks_.insert({vpn, e});
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
PageTableWalker::handle_l1d_outgoing(const Transaction& t)
{
    PTWEntry& e = ongoing_walks_.at(t.address);
    e.state = PTWState::ACCESS_DONE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
PageTableWalker::ptwc_access(size_t level, uint64_t vpn)
{
    ptwc_t& c = caches_.at(level);
    uint64_t k = vpn >> (level*numeric_traits<NUM_PTE_PER_TABLE>::log2);

    auto c_it = std::find_if(c.begin(), c.end(),
                        [k] (const PTWCEntry& e)
                        {
                            return e.valid && e.address = k;
                        });
    if (c_it == c.end()) {
        // Install `k`.
        auto v_it = std::find_if_not(c.begin(), c.end(),
                        [] (const PTWCEntry& e)
                        {
                            return e.valid;
                        });
        if (v_it == c.end()) {
            // Find victim.
            v_it = std::min_element(c.begin(), c.end(),
                        [] (const PTWCEntry& e)
                        {
                            return e.timestamp; 
                        });
        }
        v_it->valid = true;
        v_it->address = k;
        v_it->timestamp = GL_CYCLE;
        return false;
    } else {
        c_it->timestamp = GL_CYCLE;
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
page_table_byteaddr(uint8_t coreid, size_t ptw_level, uint64_t vpn)
{
    uint64_t addr_offset = 0;
    size_t entries_in_level = 1;
    for (size_t i = ptw_level; i < PTW_LEVELS; i++) {
        // Get entry-bits from vpn.
        uint64_t e = fast_mod<NUM_PTE_PER_TABLE>(vpn);
        // So there are two situations here:
        //  (1) we are at `ptw_level`: in this case, we merely increment `addr_offset` by `e`
        //  (2) we are not at `ptw_level`: then we need to account for the number of pages allocated
        //      to this level (`entries_in_level`) and increment by `entries_in_level*e` to get to the
        //      right page table/directory.
        addr_offset += (i==ptw_level) ? e : entries_in_level*(e+1)
        entries_in_level *= NUM_PTE_PER_TABLE;
        // Shift right -- next `log2(NUM_PTE_PER_TABLE)` bits will be the entry for the next level.
        vpn >>= numeric_traits<NUM_PTE_PER_TABLE>::log2;
    }
    // Add this offset to the base register.
    return GL_OS::PTBR + addr_offset;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
