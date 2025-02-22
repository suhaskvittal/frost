/*
 *  author: Suhas Vittal
 *  date:   5 February 2025
 * */

#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL>
#define __TEMPLATE_CLASS__ SamplingDeadBlockPredictor<IMPL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__TEMPLATE_CLASS__::SamplingDeadBlockPredictor()
{
    for (size_t i = 0; i < predictor_.size(); i++)
        predictor_[i].fill(CTR_DEFAULT);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ bool
__TEMPLATE_CLASS__::predict_if_dead(const Transaction& trans) const
{
    if constexpr (trace_format_does_not_support_ip())
        return false;
        
    if (trans.type == Transaction::Type::INSTRUCTION)
        return false;

    auto [ip, coreid] = get_ip_and_coreid_from(trans);

    // Make prediction:
    ctr_type sum = 0;
    if constexpr (USE_IDEAL_PREDICTOR)
    {
        if (ideal_predictor_.count(ip))
            sum = ideal_predictor_.at(ip);
    }
    else
    {
        for (size_t i = 0; i < predictor_.size(); i++)
        {
            size_t h = predictor_hash<PRED_TABLE_SIZE>(ip, coreid, i);
            sum += predictor_.at(i).at(h);
        }
    }
    return sum >= CTR_SUM_THRESHOLD;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_on_probe_or_fill(const Transaction& trans)
{
    if constexpr (trace_format_does_not_support_ip())
        return;
        
    if (trans.type == Transaction::Type::INSTRUCTION)
        return;

    size_t idx = cache_set_index<IMPL>(trans.address);
    // The assumption here is that `set_modulus_` is a power of two (which is true if `NUM_SETS` is also
    // a power of two)
    if (fast_mod<SET_MODULUS>(idx) != 0)
        return;

    auto [ip, coreid] = get_ip_and_coreid_from(trans);

    // Access the cache:
    idx >>= numeric_traits<SET_MODULUS>::log2;
    auto& s = sampler_.csets[idx];

    auto it = std::find_if(s.begin(), s.end(),
                        [addr=trans.address] (const auto& e) { return e.valid && e.address == addr; });
    bool inc = (it == s.end());
    if (it == s.end())
    {
        // Get a victim:
        it = std::find_if_not(s.begin(), s.end(),
                        [] (const auto& e) { return e.valid; });
        if (it == s.end())
        {
            // First search for a dead block:
            it = std::find_if(s.begin(), s.end(),
                        [] (const auto& e) { return e.likely_dead; });

            // Otherwise, use LRU:
            if (it == s.end())
            {
                it = std::min_element(s.begin(), s.end(),
                            [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
            }
        }
    }
    
    if (it->valid)
    {
        auto d_it = ip_store_.find(it->address);
        const auto& [v_ip, v_coreid] = d_it->second;
        update_prediction_counters(v_ip, v_coreid, inc);
        ip_store_.erase(d_it);
    }

    it->valid = true;
    it->address = trans.address;
    it->timestamp = GL_CYCLE;
    it->likely_dead = predict_if_dead(trans);

    ip_store_[trans.address] = data_store_type{ip, coreid};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_on_mark_dirty(const Transaction& trans)
{
    if constexpr (trace_format_does_not_support_ip())
        return;

    size_t idx = cache_set_index<IMPL>(trans.address);
    // The assumption here is that `set_modulus_` is a power of two (which is true if `NUM_SETS` is also
    // a power of two)
    if (fast_mod<SET_MODULUS>(idx) != 0)
        return;

    auto [ip, coreid] = get_ip_and_coreid_from(trans);

    // Access the cache:
    idx >>= numeric_traits<SET_MODULUS>::log2;
    auto& s = sampler_.csets[idx];

    auto it = std::find_if(s.begin(), s.end(),
                        [addr=trans.address] (const auto& e) { return e.valid && e.address == addr; });

    // We only care about updates if this is a hit
    if (it != s.end())
    {
        auto d_it = ip_store_.find(it->address);
        const auto& [v_ip, v_coreid] = d_it->second;
        update_prediction_counters(v_ip, v_coreid, false);

        it->likely_dead = predict_if_dead(trans);

        ip_store_[trans.address] = data_store_type(ip, coreid);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_prediction_counters(uint64_t ip, uint8_t coreid, bool inc)
{
    if constexpr (USE_IDEAL_PREDICTOR)
    {
        ctr_type& c = ideal_predictor_[ip];

        if (inc)
            ++c;
        else
            --c;

        c = std::clamp(c, CTR_MIN, CTR_MAX);
    }
    else
    {
        for (size_t i = 0; i < predictor_.size(); i++)
        {
            size_t h = predictor_hash<PRED_TABLE_SIZE>(ip, coreid, i);
            ctr_type& c = predictor_[i][h];

            if (inc)
                ++c;
            else if (i & 1)
                c >>= 1;
            else
                --c;

            c = std::clamp(c, CTR_MIN, CTR_MAX);
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::data_store_type
__TEMPLATE_CLASS__::get_ip_and_coreid_from(const Transaction& trans) const
{
    uint64_t ip = trans.ip;
    uint8_t coreid = trans.coreid;

    if (trans.is_write())
    {
        ip = ~ip;
        coreid += NUM_THREADS;
    }

    return data_store_type{ip, coreid};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t TABLE_SIZE> size_t
predictor_hash(uint64_t ip, uint8_t coreid, size_t table_idx)
{
    constexpr std::array<uint32_t, 3> MAGIC_WORDS
    {
        0xdeadbeef, 
        0xc001d00d,
        0xb00bd1ce
    };

    ip ^= (coreid) | (coreid << 8) | (coreid << 16);
    ip ^= MAGIC_WORDS.at(table_idx);

    // Rotate the bytes:
    std::vector<uint32_t> bytes
    {
        ip & 0xff,
        (ip >> 8) & 0xff,
        (ip >> 16) & 0xff
    };
    std::rotate(bytes.begin(), bytes.begin() + table_idx, bytes.end());
    
    // Join the bytes:
    uint32_t h = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
    return fast_mod<TABLE_SIZE>(h);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
