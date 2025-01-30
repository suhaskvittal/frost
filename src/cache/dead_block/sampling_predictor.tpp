/*
 *  author: Suhas Vittal
 *  date:   28 January 2025
 * */

#include "util/numerics.h"

#include <algorithm>

#define __TEMPLATE_HEADER__ template <class BASE_CACHE_TYPE>
#define __TEMPLATE_CLASS__ SamplingPredictor<BASE_CACHE_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::SamplingPredictor()
    :sampler_(new Sampler)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_on_access(uint64_t ip, uint64_t address, uint8_t coreid, bool writeback)
{
    if (fast_mod<SAMPLER_SET_GAP>(address) != 0)
        return;
    
    // Modify `ip` and `coreid` if this is writeback.
    if (writeback)
    {
        ip = ~ip;
        coreid += NUM_THREADS;
    }

    bool hit = sampler_->probe(address);

    if (hit)
    {
        const auto& [curr_ip, curr_coreid] = sampler_contents_[address];
        // Then existing `ip` for `address` should be predicted as not dead.
        update_predictor_counters(curr_ip, curr_coreid, false);
    }
    else
    {
        // Need to evict something.
        auto v = sampler_->fill(address, 1, writeback);
        if (v.has_value())
        {
            const auto& e = v.value();
            auto v_it = sampler_contents_.find(e.address);
            const auto& [v_ip, v_coreid] = v_it->second;
            update_predictor_counters(v_ip, v_coreid, true);
            sampler_contents_.erase(v_it);
        }
    }
    if (predict(ip, address, coreid, false) == DeadBlockPrediction::LIKELY_DEAD)
        sampler_->mark_likely_dead(address);
    sampler_contents_[address] = sampler_data_type{ip, coreid};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ DeadBlockPrediction
__TEMPLATE_CLASS__::predict(uint64_t ip, uint64_t address, uint8_t coreid, bool writeback) const
{
    // Modify `ip` and `coreid` if this is writeback.
    if (writeback)
    {
        ip = ~ip;
        coreid += NUM_THREADS;
    }

    int8_t tot = 0;
    for (size_t i = 0; i < 3; i++)
    {
        const auto& pt = predictor_tables_.at(i);
        tot += pt.at(hash(ip, coreid, i));
    }
    
    if (tot < PREDICTOR_LOWER_THRESHOLD)
        return DeadBlockPrediction::LIKELY_ALIVE;
    else if (tot >= PREDICTOR_UPPER_THRESHOLD)
        return DeadBlockPrediction::LIKELY_DEAD;
    else
        return DeadBlockPrediction::UNSURE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_predictor_counters(uint64_t ip, uint8_t coreid, bool inc)
{
    constexpr int8_t CTR_MAX = (1<<PREDICTOR_WIDTH)-1;

    for (size_t i = 0; i < 3; i++)
    {
        auto& pt = predictor_tables_[i];
        int8_t& bits = pt[hash(ip, coreid, i)];

        if (i & 1)
            bits = inc ? (bits+1) : (bits>>1);
        else
            bits = inc ? (bits+1) : (bits-1);
        bits = std::clamp(bits, static_cast<int8_t>(0), CTR_MAX);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline uint32_t mix(uint32_t a, uint32_t b, uint32_t c)
{
    a -= b;
    a -= c;
    a ^= (c >> 13);
    b -= a;
    b -= c;
    b ^= (a << 8);
    c -= a;
    c -= b;
    c ^= (b >> 13);
    return c;
}

__TEMPLATE_HEADER__ inline uint16_t
__TEMPLATE_CLASS__::hash(uint64_t ip, uint8_t coreid, size_t table_idx) const
{
    uint32_t x = ip ^ (coreid << 2);
    uint32_t h = mix(0xfeedface, 0xdeadb10c, x) + (mix(0xc001d00d, 0xfade2b1c, x) >> table_idx);
    return static_cast<uint16_t>(fast_mod<PREDICTOR_ENTRIES>(h));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
