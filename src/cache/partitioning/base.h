/*
 *  author: Suhas Vittal
 *  date:   20 February 2025
 * */

#ifndef CACHE_PARTITIONING_BASE_h
#define CACHE_PARTITIONING_BASE_h

#include <array>
#include <cstdint>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern uint64_t OPT_CACHE_PARTITION_UPDATE_CYCLES;

extern uint64_t GL_CYCLE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

using cache_partition_array = std::array<size_t, NUM_THREADS>;

class PartitionManagerBase
{
public:
    uint64_t last_update_cycle_ =0;
public:
    using part_iterator = cache_partition_array::iterator;

    virtual void initialize_partition(part_iterator begin, part_iterator end)
    {
        std::fill(begin, end, std::numeric_limits<size_t>::max());
    }

    virtual void update_partition(part_iterator begin, part_iterator end){}
    virtual void update_on_probe(const Transaction&) {}
    virtual void update_on_mark_dirty(const Transaction&) {}
    virtual void update_on_fill(const Transaction&) {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

using NoPartitionManager = PartitionManagerBase;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Since the implementation of static (or, a communist) partitioning is straightforward, we will
 * include it here:
 * */
template <class IMPL>
class CommunistPartitionManager : public PartitionManagerBase
{
public:
    inline void initialize_partition(part_iterator begin, part_iterator end) override
    {
        std::fill(begin, end, IMPL::NUM_WAYS/NUM_THREADS);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_PARTITIONING_BASE_h
