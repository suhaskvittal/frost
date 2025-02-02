#ifndef DRAM_ENUMS_h
#define DRAM_ENUMS_h

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMPagePolicy
{
    OPEN,
    CLOSE,
    HYBRID
};

enum class DRAMSchedPolicy
{
    FCFS,       // first come first serve
    FRFCFS,     // row-hits, then fcfs -- has demand precharge to ensure some fairness
    FRFCFS_WP   // FRFCFS that obeys priority -- commands will not be issued if there is another
                // command with higher priority (see `transaction.h`)
};

enum class DRAMWritePolicy
{
    ASYNC,  // Writes are issued when they are ready
    SYNC    // Writes are issued altogether by each command queue
};

enum class DRAMClosureHint
{
    NONE,
    KEEP_OPEN,
    CLOSE_AFTER
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline constexpr bool dram_sched_prioritize_row_buffer_hits(DRAMSchedPolicy p)
{
    return p == DRAMSchedPolicy::FRFCFS || p == DRAMSchedPolicy::FRFCFS_WP;
}

inline constexpr bool dram_sched_obey_issue_priority(DRAMSchedPolicy p)
{
    return p == DRAMSchedPolicy::FRFCFS_WP;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_ENUMS_h
