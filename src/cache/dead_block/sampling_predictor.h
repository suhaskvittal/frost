/*
 *  author: Suhas Vittal
 *  date:   28 January 2025
 * */

#ifndef CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h
#define CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h

#include "cache.h"
#include "cache/dead_block/base_predictor.h"

#include <array>
#include <memory>
#include <type_traits>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class BASE_CACHE_TYPE>
class SamplingPredictor : public DeadBlockPredictor
{
public:
private:
    constexpr static size_t SAMPLER_SETS = 32*NUM_THREADS;
    constexpr static size_t SAMPLER_ASSOC = 16;
    constexpr static CacheReplPolicy SAMPLER_REPL = CacheReplPolicy::LRU;
    constexpr static size_t SAMPLER_SET_GAP = BASE_CACHE_TYPE::num_sets() / SAMPLER_SETS;

    struct Sampler : Cache<SAMPLER_SETS, SAMPLER_ASSOC, SAMPLER_REPL>
    {
        inline size_t get_set_index(uint64_t x) const override
        {
            return fast_mod<SAMPLER_SETS>(x >> numeric_traits<SAMPLER_SET_GAP>::log2);
        }
    };

    constexpr static size_t PREDICTOR_WIDTH = 2;
    constexpr static size_t PREDICTOR_ENTRIES = (1 << 14) * NUM_THREADS;
    constexpr static int8_t PREDICTOR_LOWER_THRESHOLD = 2;
    constexpr static int8_t PREDICTOR_UPPER_THRESHOLD = 8;

    using sampler_ptr = std::unique_ptr<Sampler>;
    using sampler_data_type = std::tuple<uint64_t, uint8_t>;  // <PC, thread-id>
    using sampler_data_map_type = std::unordered_map<uint64_t, sampler_data_type>;

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
    predictor_table_array_type predictor_tables_{};
public:
    SamplingPredictor(void);

    void update_on_access(uint64_t ip, uint64_t address, uint8_t coreid) override;
    void handle_writeback(uint64_t address) override;

    DeadBlockPrediction predict(uint64_t ip, uint64_t address, uint8_t coreid) const override;
private:
    void update_predictor_counters(uint64_t ip, uint8_t coreid, bool inc);

    uint16_t hash(uint64_t ip, uint8_t coreid, size_t table_idx) const;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class> struct is_sampling_predictor : std::false_type {};
template <class B> struct is_sampling_predictor<SamplingPredictor<B>> : std::true_type {};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "sampling_predictor.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_DEAD_BLOCK_SAMPLING_PREDICTOR_h

