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

    virtual void update_partition(part_iterator begin, part_iterator end) =0;
    virtual void update_on_access(const Transaction&) =0;
    virtual void update_on_fill(const Transaction&) =0;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class NoPartitionManager : public PartitionManagerBase
{
    inline void update_partition(part_iterator, part_iterator) override { last_update_cycle_ = GL_CYCLE; }
    inline void update_on_access(const Transaction&) override {};
    inline void update_on_fill(const Transaction&) override {};
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_PARTITIONING_BASE_h
