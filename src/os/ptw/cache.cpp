/*
 *  author: Suhas Vittal
 *  date:   8 December 2024
 * */

#include "constants.h"
#include "globals.h"

#include "os/ptw/cache.h"
#include "util/numerics.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

PTWCache::PTWCache(size_t num_sets, size_t num_ways)
    :num_sets_(num_sets),
    num_ways_(num_ways),
    csets_(num_sets_, cset_t(num_ways))
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
PTWCache::probe_and_fill_on_miss(uint64_t bits)
{
    ++s_accesses_;

    size_t idx = bits & (num_sets_-1);
    cset_t& s = csets_.at(idx);
    // Probe:
    auto s_it = std::find_if(s.begin(), s.end(),
                        [bits] (const PTWCacheEntry& e)
                        {
                            return e.valid && e.bits == bits;
                        });
    if (s_it == s.end()) {
        // Handle miss:
        ++s_misses_;
        // Find victim:
        auto v_it = std::find_if_not(s.begin(), s.end(),
                        [] (const PTWCacheEntry& e) { return e.valid; });
        if (v_it == s.end()) {
            v_it = std::min_element(s.begin(), s.end(),
                        [] (const auto& x, const auto& y)
                        {
                            return x.timestamp < y.timestamp;
                        });
        }
        v_it->valid = true;
        v_it->bits = bits;
        v_it->timestamp = GL_CYCLE;
        return false;
    } else {
        s_it->timestamp = GL_CYCLE;
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

size_t
ptwc_get_initial_directory_level(ptwc_array_t& caches, uint64_t vpn)
{
    size_t mask_bits = numeric_traits<NUM_PTE_PER_TABLE>::log2,
           off = (PT_LEVELS-1) * numeric_traits<NUM_PTE_PER_TABLE>::log2;
    // Access all caches simultaneously.
    size_t last_hit = PT_LEVELS-1;
    for (int i = caches.size()-1; i >= 0; i--) {
        ptwc_ptr& c = caches.at(i);
        uint64_t b = (vpn >> off) & ((1L << mask_bits) - 1);

        if (c->probe_and_fill_on_miss(b)) {
            last_hit = static_cast<size_t>(i);
        }

        mask_bits += numeric_traits<NUM_PTE_PER_TABLE>::log2;
        off -= numeric_traits<NUM_PTE_PER_TABLE>::log2;
    }
    return last_hit;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
