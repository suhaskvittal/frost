#ifndef DRAM_ENUMS_h
#define DRAM_ENUMS_h

enum class DRAMPagePolicy
{
    OPEN,
    CLOSE
};

enum class DRAMSchedPolicy
{
    FCFS,       // first come first serve
    FRFCFS,     // row-hits, then fcfs -- has demand precharge to ensure some fairness
};

enum class DRAMWritePolicy
{
    ASAP,       // Writes are finished in their command queue order.
    ALAP,       // Writes are only issued if `MAX_WRITES` is reached
    ALAP_SYNC   // Writes are issued if any command queue reaches `MAX_WRITES`
};

#endif  // DRAM_ENUMS_h
