/*
 *  author: Suhas Vittal
 *  date:   24 February 2025
 * */

#ifndef CACHE_EXT_ATD_h
#define CACHE_EXT_ATD_h

#include "cache/entry.h"
#include "cache/indexing.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern uint64_t GL_CYCLE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class ATDLookupResult { HIT, MISS, IGNORED };

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, size_t NUM_SETS>
struct AuxTagDirectory
{
    constexpr static size_t SET_MODULUS = IMPL::NUM_SETS / NUM_SETS;

    cset_array csets;
    
    AuxTagDirectory(void)
        :csets(NUM_SETS, cset_type(IMPL::NUM_WAYS, CacheEntry{}))
    {}
    /*
     * For extensibility, we provide a callback argument for each function that accesses
     * the ATD. Callback syntax:
     *  (1) `probe` and `mark_dirty`: callback will be given a const `cset_type` reference
     *      and a `const_iterator` to the entry. On a miss, the iterator points to the end
     *      of the set.
     *  (2) `fill` will be given a const `cset_type` reference and a `CacheEntry`. This
     *      entry is valid iff there is a victim.
     * */ 
    template <class CALLBACK_TYPE>
    ATDLookupResult probe(const Transaction&, const CALLBACK_TYPE&);

    template <class CALLBACK_TYPE>
    ATDLookupResult mark_dirty(const Transaction&, const CALLBACK_TYPE&);

    template <class CALLBACK_TYPE>
    CacheEntry fill(const Transaction&, const CALLBACK_TYPE&);
private:
    cset_array::iterator set_lookup(const Transaction&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "atd.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_SET_ATD_h
