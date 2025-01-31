/*
 *  author: Suhas Vittal
 *  date:   31 January 2025
 * */

#ifndef CACHE_OTHER_IMPL_DRAM_ADDRESS_AWARE_CACHE_h
#define CACHE_OTHER_IMPL_DRAM_ADDRESS_AWARE_CACHE_h

#include "cache.h"
#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern size_t OPT_DRAM_ADDRESS_AWARE_CACHE_SKIP_BITS;
extern size_t OPT_DRAM_ADDRESS_AWARE_CACHE_NUM_BITS;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<SETS,WAYS,POL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t SETS, size_t WAYS, CacheReplPolicy POL>
class DRAMAddressAwareCache : public Cache<SETS, WAYS, POL>
{
public:
    using __TEMPLATE_PARENT__::Cache;

    static size_t _get_set_index(uint64_t x)
    {
        const size_t skip_count = OPT_DRAM_ADDRESS_AWARE_CACHE_SKIP_BITS;
        const size_t col_count = OPT_DRAM_ADDRESS_AWARE_CACHE_NUM_BITS;

        size_t idx = 0;

        // Initialize dram column bit positions
        size_t idx_pos = 0;
        size_t prev_pos = 0;
        for (size_t i = skip_count; i < skip_count+col_count; i++)
        {
            size_t pos = dram_col_bit_index(i);
            size_t pos_diff = pos-prev_pos;
            idx |= ((x >> prev_pos) & ((1L << pos_diff)-1)) << idx_pos;
            idx_pos += pos_diff;
            prev_pos = pos+1;
        }
        idx |= (x >> prev_pos) << idx_pos;
        return fast_mod<SETS>(idx);
    }
private:
    inline size_t get_set_index(uint64_t x) const override
    {
        return DRAMAddressAwareCache::_get_set_index(x);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_DRAM_ADDRESS_AWARE_CACHE_h
