/* author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef OS_h
#define OS_h

#include "constants.h"
#include "instruction.h"
#include "complex_model/os/ptw.h"
#include "complex_model/os/vmem.h"
#include "os/free_list.h"

#include <array>
#include <iosfwd>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct ITLB;
struct DTLB;
struct L2LTB;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class OS
{
public:
    using itlb_ptr  = std::unique_ptr<ITLB>;
    using dtlb_ptr  = std::unique_ptr<DTLB>;
    using l2tlb_ptr = std::unique_ptr<L2TLB>;

    using itlb_array_t = std::array<itlb_ptr, NUM_THREADS>;
    using dtlb_array_t = std::array<dtlb_ptr, NUM_THREADS>;
    using l2tlb_array_t = std::array<l2tlb_ptr, NUM_THREADS>;

    itlb_array_t  ITLB_;
    dtlb_array_t  DTLB_;
    l2tlb_array_t L2TLB_;

    FreeList free_list_{};
private:
    using vmem_ptr = std::unique_ptr<VirtualMemory>;
    using ptw_ptr = std::unique_ptr<PageTableWalker>;
    using vmem_array_t = std::array<vmem_ptr, NUM_THREADS>;
    using ptw_array_t = std::array<ptw_ptr, NUM_THREADS>;
    /*
     * Each core has its own virtual memory and page walker (`CoreMMU`):
     * */
    vmem_array_t vmem_;
    ptw_array_t  ptw_;
public:
    using ptwc_init_list_t = PageTableWalker::ptwc_init_list_t;
        
    OS(ptwc_init_list_t);

    uint64_t warmup_translate_ip(uint8_t coreid, uint64_t byteaddr);
    uint64_t warmup_translate_ldst(uint8_t coreid, uint64_t lineaddr);

    void tick(void);

    bool translate_ip(uint8_t coreid, iptr_t);
    bool translate_ldst(uint8_t coreid, iptr_t, uint64_t);

    void print_stats(std::ostream&);
    /*
     * Inlines:
     * */
    inline void handle_l1d_outgoing(uint8_t coreid, const Transaction& t)
    {
        ptw_[coreid]->handle_l1d_outgoing(t);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_h
