/*
 *  author: Suhas Vittal
 *  date:   5 February 2025
 * */

#ifndef CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h
#define CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h

#include "constants.h"
#include "cache/entry.h"

#include <array>
#include <cstdint>
#include <cstddef>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL>
class SamplingDeadBlockPredictor
{
public:
private:
    /*
     * Definition of internal sampler cache. We just need a simple implementation
     * for our purposes:
     * */
    struct internal_cache_type
    {
        constexpr static size_t NUM_SETS = 64 * NUM_THREADS;
        constexpr static size_t NUM_WAYS = 13;

        using cset_type = std::array<CacheEntry, NUM_WAYS>;
        using cset_array = std::array<cset_type, NUM_SETS>;

        cset_array csets{};
    };

    constexpr static size_t PRED_TABLE_SIZE = (1L << 12) * NUM_THREADS;
    constexpr static size_t SET_MODULUS = IMPL::NUM_SETS / internal_cache_type::NUM_SETS;

    using ctr_type = int8_t;
    using pred_table_type = std::array<ctr_type, PRED_TABLE_SIZE>;
    using pred_table_array = std::array<pred_table_type, 3>;

    using data_store_type = std::pair<uint64_t, uint8_t>;  // ip, coreid
    using data_store_map = std::unordered_map<uint64_t, data_store_type>;

    constexpr static size_t CTR_WIDTH = 2;
    constexpr static ctr_type CTR_MIN = 0;
    constexpr static ctr_type CTR_MAX = (1<<CTR_WIDTH)-1;
    constexpr static ctr_type CTR_DEFAULT = 1;
    constexpr static ctr_type CTR_SUM_THRESHOLD = 8;
    /*
     * Structures for predicting dead blocks
     * */
    internal_cache_type sampler_;
    pred_table_array    predictor_;
    data_store_map      ip_store_;
    /*
     * These are the number of sets that the larger cache has:
     * */
    const size_t global_sets_;
    const size_t set_modulus_;
public:
    SamplingDeadBlockPredictor(void);

    bool predict_if_dead(const Transaction&) const;
    void update_on_probe_or_fill(const Transaction&);
    void update_on_mark_dirty(const Transaction&);
private:
    void update_prediction_counters(uint64_t ip, uint8_t coreid, bool inc);
    data_store_type get_ip_and_coreid_from(const Transaction&) const;

};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t TABLE_SIZE>
size_t predictor_hash(uint64_t ip, uint8_t coreid, size_t table_idx);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr bool trace_format_does_not_support_ip(void)
{
#if defined(TRACE_FORMAT_MTF)
    return true;
#else
    return false;
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "sampling_predictor.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif   // CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h
