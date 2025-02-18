/*
 *  author: Suhas Vittal
 *  date:   3 February 2025
 * */

#include "simple_cache.h"

#include <algorithm>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SimpleCache::SimpleCache(size_t assoc, size_t sets)
    :assoc_(assoc),
    sets_(sets),
    csets_(sets, std::vector<Entry>(assoc))
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
SimpleCache::probe(uint64_t address, bool write)
{
    cset_type& s = get_set(address);
    auto it = std::find_if(s.begin(), s.end(),
                    [address] (const auto& e) { return e.valid && e.address == address; });
    if (it == s.end())
        return false;
    else
    {
        it->timestamp = s_count_;
        it->dirty |= write;
        ++s_count_;
        return true;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
SimpleCache::mark(uint64_t address, bool dirty)
{
    cset_type& s = get_set(address);
    auto it = std::find_if(s.begin(), s.end(),
                    [address] (const auto& e) { return e.valid && e.address == address; });
    if (it == s.end())
        return false;
    else
    {
        it->dirty = dirty;
        return true;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

SimpleCache::victim_type
SimpleCache::fill(uint64_t address, bool dirty)
{
    victim_type out;

    cset_type& s = get_set(address);
    auto it = std::find_if_not(s.begin(), s.end(),
                            [] (const auto& e) { return e.valid; });
    if (it == s.end())
    {
        it = std::min_element(s.begin(), s.end(),
                        [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });

        // only record to miss trace if there is a victim
        // fmt: access count (s_count), incoming, outgoing
        if (record_miss_trace_)
        {
            fwrite(&s_count_, 4, 1, miss_trace_);
            fwrite(&it->address, 8, 1, miss_trace_);
            fwrite(&address, 8, 1, miss_trace_);
        }

        out.emplace(std::move(*it));
    }
    it->valid = true;
    it->dirty = dirty;
    it->address = address;
    it->timestamp = s_count_;
    ++s_count_;

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
