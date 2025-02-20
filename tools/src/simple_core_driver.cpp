/*
 *  author: Suhas Vittal
 *  date:   18 February 2025
 * */

#include "simple_core_driver.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SimpleCoreDriver::SimpleCoreDriver(std::string f, SimpleCache&& L1I, SimpleCache&& L1D, SimpleCache&& L2, SimpleCache&& L3)
    :trace_file(f),
    trace_reader(f),
    l1i_cache(std::move(L1I)),
    l1d_cache(std::move(L1D)),
    l2_cache(std::move(L2)),
    l3_cache(std::move(L3))
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SimpleCoreDriver::step(bool warmup)
{
    // Get next instruction from trace reader.
    Instruction* inst = new Instruction(0, trace_reader());
    if (!inst->is_mem_inst() || trace_reader.eof_)
    {
        delete inst;
        return;
    }

    // icache access:
    uint64_t ip_line = inst->ip >> 6;
    l1_access(l1i_cache, ip_line, false);

    // dcache access:
    for (const auto& op : inst->loads.args)
        l1_access(l1d_cache, op.vla, false);
    for (const auto& op : inst->stores.args)
        l1_access(l1d_cache, op.vla, true);

    delete inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SimpleCoreDriver::l1_access(SimpleCache& c, uint64_t address, bool write)
{
    auto result = c.probe(address, write);
    if (result == SimpleCache::ProbeResultType::MISS)
    {
        // Do access to l2 -- either demand read or write-allocate
        l2_access(address, false);
        auto victim = c.fill(address, write);

        // Do writeback if dirty line is evicted
        if (victim.has_value() && victim.value().dirty)
            l2_access(victim.value().address, true);
    }
    else if (result == SimpleCache::ProbeResultType::HIT_BUT_DO_FILL)
    {
        direct_fill(address, l2_cache);
        direct_fill(address, l3_cache);
    }
}

void
SimpleCoreDriver::l2_access(uint64_t address, bool write)
{
    auto result = write ? l2_cache.mark_dirty(address) : l2_cache.probe(address);
    if (result == SimpleCache::ProbeResultType::MISS)
    {
        // Access L3 if this is not a writeback:
        if (!write)
            l3_access(address, false);

        // Fill in address regardless:
        auto victim = l2_cache.fill(address, write);
        
        // Do writeback if dirty line is evicted
        if (victim.has_value() && victim.value().dirty)
            l3_access(victim.value().address, true);
    }
    else if (result == SimpleCache::ProbeResultType::HIT_BUT_DO_FILL)
    {
        direct_fill(address, l3_cache);
    }
}

void
SimpleCoreDriver::l3_access(uint64_t address, bool write)
{
    auto result = write ? l3_cache.mark_dirty(address) : l3_cache.probe(address);
    if (result == SimpleCache::ProbeResultType::MISS)
    {
        // Victim can be discarded -- no dram
        l3_cache.fill(address, write);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
SimpleCoreDriver::direct_fill(uint64_t address, SimpleCache& c)
{
    c.fill(address, false);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
