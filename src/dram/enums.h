#ifndef DRAM_ENUMS_h
#define DRAM_ENUMS_h

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

#endif  // DRAM_ENUMS_h
