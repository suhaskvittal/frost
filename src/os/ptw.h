/*
 *  author: Suhas Vittal
 *  date:   7 December 2024
 * */

#ifndef OS_PTW_h
#define OS_PTW_h
// Constants to define:
//  size_t PTW_LEVELS
//  std::array PTWC_ENTRIES
//  size_t PTESIZE;
//  uint64_T OS::PTBR

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t NUM_PTE_PER_TABLE = PAGESIZE/PTESIZE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class Transaction;
class L2TLB;
class L2Cache;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct PTWCEntry
{
    bool     valid =false;
    uint64_t address;
    uint64_t timestamp =0;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class PTWState { NEED_ACCESS, WAITING_ON_ACCESS, ACCESS_DONE };

struct PTWEntry
{
    size_t level =PTW_LEVELS-1;
    PTWState state =PTWState::NEED_ACCESS;
    uint64_t pt_address;
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
    /*
     * Page table walker cache (PTWC) maps bits in the vpn
     * to the physical address of page table directory address.
     * */
    using ptwc_t = std::vector<PTWCEntry>;
    using ptwc_array_t = std::array<ptwc_t, PT_WALK_LEVELS>;
    /*
     * Maps each vpn undergoing a page walk to the current level in the walk.
     * */
    using ptw_tracker_t = std::unordered_map<uint64_t, PTWEntry>;
    /*
     * Both caches are references from `Core` and `OS`
     * */
    l2tlb_ptr&   L2TLB_;
    l1d_ptr&     L1D_;

    ptwc_array_t caches_{};
    ptw_tracker_t ongoing_walks_;
public:
    PageTableWalker(l2tlb_ptr&, l1d_ptr&);

    void tick(void);
    void start_walk(uint64_t vpn);
    void handle_l1d_outgoing(Transaction);
private:
    bool ptwc_access(size_t level, uint64_t vpn);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Instead of managing a bunch of addresses for page tables, we fix the
 * location of the page directories/tables in the physical memory to 
 * `PTBR` plus some size, which depends on `PTW_LEVELS`.
 *
 * For `PTW_LEVELS == 4`, this would be 2**27 + 2**18 + 2**9 + 1 pages
 *
 * */
uint64_t page_table_byteaddr(uint8_t coreid, size_t ptw_level, uint64_t vpn);
uint64_t page_table_lineaddr(uint8_t coreid, size_t ptw_level, uint64_T vpn);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_PTW_h
