/*
 *  author: Suhas Vittal
 *  date:   7 February 2025
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class ITER> inline ITER
cset_find(uint64_t x, ITER begin, ITER end)
{
    return std::find_if(begin, end, 
                [x] (const auto& e) { return e.valid && e.address == x; });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class ITER> inline size_t
cset_get_lru_position_of_entry(const CacheEntry& e, ITER begin, ITER end)
{
    return std::count_if(begin, end,
                [t=e.timestamp] (const auto& x) { return t > x.timestamp; });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class ITER> ITER
cset_get_way_in_lru_position(ITER begin, ITER end, size_t pos)
{
    if (pos == 0)   // O(n)
    {
        return std::min_element(begin, end,
                    [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
    }
    else            // O(n^2)
    {
        return std::find_if(begin, end,
                    [pos, &begin, &end] 
                    (const auto& e) { return pos == cset_get_lru_position_of_entry(e, begin, end); }); 
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
