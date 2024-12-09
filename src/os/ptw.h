/*
 *  author: Suhas Vittal
 *  date:   7 December 2024 */

#ifndef OS_PTW_h
#define OS_PTW_h

#include "os/vmem.h"
#include "os/ptw/cache.h"
#include "util/numerics.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class Transaction;
struct L2TLB;
struct L1DCache;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class PTWState { NEED_ACCESS, WAITING_ON_ACCESS };

struct PTWEntry
{
    using walk_data_t = VirtualMemory::walk_result_t;

    size_t      curr_level =PT_LEVELS-1;
    PTWState    state =PTWState::NEED_ACCESS;
    walk_data_t walk_data;
    size_t      curr_walk_data_idx = 1;

    inline uint64_t get_curr_pfn_lineaddr(void) const
    {
        uint64_t pfn = walk_data.at(curr_walk_data_idx),
                 off = walk_data.at(curr_walk_data_idx+1);
        uint64_t paddr = (pfn << numeric_traits<PAGESIZE>::log2) | off;
        return paddr >> numeric_traits<LINESIZE>::log2;
    }

    inline void next(void)
    {
        --curr_level;
        curr_walk_data_idx += 2;
        state = PTWState::NEED_ACCESS;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class PageTableWalker
{
public:
    /*
     * As the `L2TLB` will send misses to the page table walker,
     * we need to have a mimic of `IOBus` here.
     * */
    struct IO
    {
        PageTableWalker* owner;

        IO(PageTableWalker*);
        bool add_incoming(Transaction);
    };

    const uint8_t coreid_;
private:
    using l2tlb_ptr = std::unique_ptr<L2TLB>;
    using l1d_ptr   = std::unique_ptr<L1DCache>;
    using vmem_ptr  = std::unique_ptr<VirtualMemory>;
    using ptw_tracker_t = std::unordered_map<uint64_t, PTWEntry>;
    /*
     * Both caches are references from `Core` and `OS`.
     * */
    l2tlb_ptr&   L2TLB_;
    l1d_ptr&     L1D_;
    /*
     * Reference from `OS`.
     * */
    vmem_ptr& vmem_;

    ptwc_array_t  caches_;
    ptw_tracker_t ongoing_walks_;
public:
    using ptwc_init_params_t = std::tuple<size_t, size_t>;
    using ptwc_init_array_t = std::array<ptwc_init_params_t, PT_LEVELS-1>;

    PageTableWalker(uint8_t coreid, l2tlb_ptr&, l1d_ptr&, vmem_ptr&, const ptwc_init_array_t&);
    /*
     * This `warmup_access` method mimics the same method found in `CacheControl (control.tpp)`.
     * Note that the second argument, `write`, is unused.
     * */
    void warmup_access(uint64_t vpn, bool);

    void tick(void);
    void handle_tlb_miss(const Transaction&);
    void handle_l1d_outgoing(const Transaction&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_PTW_h
