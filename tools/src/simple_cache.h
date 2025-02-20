/*
 *  author: Suhas Vittal
 *  date:   3 February 2025
 * */

#ifndef SIMPLE_CACHE_h
#define SIMPLE_CACHE_h

#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class SimpleCache
{
public:
    uint64_t s_accesses_ =0;
    uint64_t s_misses_ =0;
        
    struct Entry
    {
        bool valid =false;
        bool dirty =false;
        uint64_t address;
        uint64_t timestamp;
    };

    bool enable_memento_test_ =false;

    const size_t assoc_;
    const size_t sets_;
private:
    using cset_type = std::vector<Entry>;
    using cset_array = std::vector<cset_type>;

    cset_array csets_;

    uint32_t init_count_ =0;

    bool record_miss_trace_ =false;
    gzFile miss_trace_;
    /*
     * Testing stuff:
     * */
    using address_map_type = std::unordered_map<uint64_t, uint64_t>;
    /*
     * `memento`: idea is that on a probe, if the LRU way had evicted the requested address,
     * we return a hit (simulates a perfect prefetch)
     * */
    address_map_type memento_eviction_map_;
public:
    enum class ProbeResultType { HIT, MISS, HIT_BUT_DO_FILL };

    using victim_type = std::optional<Entry>;

    SimpleCache(size_t assoc, size_t sets);
    ~SimpleCache(void)
    {
        if (record_miss_trace_)
            gzclose(miss_trace_);
    }

    ProbeResultType probe(uint64_t address, bool write=false);
    ProbeResultType mark_dirty(uint64_t address);

    victim_type fill(uint64_t address, bool dirty);
    /*
     * Data collection:
     * */
    inline void start_recording_miss_trace(std::string output_file)
    {
        miss_trace_ = gzopen(output_file.c_str(), "w");
        record_miss_trace_ = true;
    }
private:
    cset_type get_lru_ways(const cset_type& s, size_t num_ways) const;

    inline size_t set_index(uint64_t address)
    {
        return address & (sets_-1);
    }

    inline cset_type& get_set(uint64_t address)
    {
        return csets_[set_index(address)];
    }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif
