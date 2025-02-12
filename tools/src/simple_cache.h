/*
 *  author: Suhas Vittal
 *  date:   3 February 2025
 * */

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class SimpleCache
{
public:
    struct Entry
    {
        bool valid =false;
        bool dirty =false;
        uint64_t address;
        uint64_t timestamp;
    };

    const size_t assoc_;
    const size_t sets_;
private:
    using cset_type = std::vector<Entry>;
    using cset_array = std::vector<cset_type>;

    cset_array csets_;

    size_t s_count_ =0;
public:
    using victim_type = std::optional<Entry>;

    SimpleCache(size_t assoc, size_t sets);

    bool probe(uint64_t address, bool write=false);
    bool mark(uint64_t address, bool dirty);

    victim_type fill(uint64_t address, bool dirty);
private:
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
