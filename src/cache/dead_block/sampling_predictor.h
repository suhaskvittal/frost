/*
 *  author: Suhas Vittal
 *  date:   28 January 2025
 * */

#ifndef CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h
#define CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h

#include "cache.h"

#include <array>
#include <memory>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class SamplingPredictor : public DeadBlockPredictor
{
public:
private:
    constexpr static size_t SAMPLER_SETS = 32;
    constexpr static size_t SAMPLER_ASSOC = 12;
    constexpr static CacheReplPolicy SAMPLER_REPL = CacheReplPolicy::LRU;

    constexpr static size_t PREDICTOR_WIDTH = 2;
    constexpr static size_t PREDICTOR_ENTRIES = 4096;
    constexpr static int8_t PREDICTOR_THRESHOLD = 8;

    using sampler_type = Cache<SAMPLER_SETS, SAMPLER_ASSOC, SAMPLER_REPL>;
    using sampler_ptr = std::unique_ptr<sampler_type>;
    using sampler_data_map_type = std::unordered_map<uint64_t, uint16_t>;

    using predictor_table_type = std::array<int8_t, PREDICTOR_ENTRIES>;
    using predictor_table_array_type = std::array<predictor_table_type, 3>;
    /*
     * `sampler_`: maintains tag array for tracking dead blocks. Corresponds to a subset of the larger
     *              cache
     * `sampler_contents_`: maps each tag in `sampler_` to a PC
     * `predictor_tables_`: actually responsible for predicting whether or not a block is dead
     * */
    sampler_ptr sampler_{};
    sampler_data_map_type sampler_contents_;
    predictor_table_array_type predictor_tables_;
public:
    SamplingPredictor(void);

    void update_on_probe(uint64_t ip, uint64_t address, size_t set_idx) override;
    bool predict_if_dead(uint64_t ip, uint64_t address) const override;
private:
    bool is_set_tracked_by_sampler(size_t idx) const;

    void update_predictor_counters(uint64_t ip, uint64_t address, bool inc);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h

