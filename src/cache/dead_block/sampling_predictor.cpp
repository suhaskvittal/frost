/*
 *  author: Suhas Vittal
 *  date:   28 January 2025
 * */

#include "cache/dead_block/sampling_predictor.h"
#include "util/numerics.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline uint16_t
hash_one(uint64_t address, uint16_t pc)
{
    return (address ^ pc) & ((1 << 12)-1);
}

inline uint16_t
hash_two(uint64_t address, uint16_t pc)
{
    uint64_t x = hash_one(address, pc);
    uint16_t upper_3_bits = x >> 12,
             lower_12_bits = x & ((1 << 12)-1);
    return lower_12_bits ^ (upper_3_bits << 4);
}

inline uint16_t
hash_three(uint64_t address, uint16_t pc)
{
    uint64_t x = hash_one(address, pc);
    uint16_t upper_3_bits = x >> 12,
             lower_12_bits = x & ((1 << 12)-1);
    uint16_t u1 = upper_3_bits & 1,
             u2 = (upper_3_bits>>1) & 1,
             u3 = (upper_3_bits>>2) & 1;
    return lower_12_bits ^ (u1 << 1) ^ (u2 << 9) ^ (u3 << 4) ^ (u4 << 11);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

SamplingPredictor::SamplingPredictor()
{
    // Initialize predictor tables:
    for (auto& pt : predictor_tables_)
        pt.fill(0b10);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
SamplingPredictor::update_on_probe(uint64_t ip, uint64_t address, size_t set_idx)
{
    if (!is_set_tracked_by_sampler(set_idx))
        return;

    if (sampler_->probe(address))
        // Then existing `ip` for `address` should be predicted as not dead.
        update_predictor_counters(sampler_contents_[address], address, true);
    else
    {
        // Need to evict something.
        auto v = sampler_->fill(address, 1, false);
        if (v.has_value())
        {
            const auto& e = v.value();
            auto v_it = sampler_contents_.find(e.address);
            update_predictor_counters(*v_it, e.address, false);
            sampler_contents_.erase(v_it);
        }
    }
    sampler_contents_[address] = ip;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
SamplingPredictor::predict_if_dead(uint64_t ip, uint64_t address) const
{
    std::array<uint16_t, 3> hashes
    {
        hash_one(ip, address),
        hash_two(ip, address),
        hash_three(ip, address)
    };

    int8_t tot = 0;
    for (size_t i = 0; i < 3; i++)
    {
        const auto& pt = predictor_tables_.at(i);
        tot += pt.at(hashes[i]);
    }
    return tot >= PREDICTOR_THRESHOLD;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
SamplingPredictor::is_set_tracked_by_sampler(size_t idx) const
{
    size_t grp = idx >> numeric_traits<SAMPLER_SETS>::log2,
           off = fast_mod<SAMPLER_SETS>(idx);
    return grp == off;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
SamplingPredictor::update_predictor_counters(uint64_t ip, uint64_t address, bool inc)
{
    std::array<uint16_t, 3> hashes
    {
        hash_one(ip, address),
        hash_two(ip, address),
        hash_three(ip, address)
    };

    for (size_t i = 0; i < 3; i++)
    {
        auto& pt = predictor_tables_[i];
        int8_t& bits = pt[hashes[i]];

        bits = inc ? (bits+1) : (bits-1);
        std::clamp(bits, static_cast<int8_t>(0), static_cast<int8_t>((1 << PREDICTOR_WIDTH)-1));
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
