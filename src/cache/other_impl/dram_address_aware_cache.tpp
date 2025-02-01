/*
 *  author: Suhas Vittal
 *  date:   31 January 2025
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <size_t SETS, size_t WAYS, CacheReplPolicy POL>
#define __TEMPLATE_CLASS__  DRAMAddressAwareCache<SETS,WAYS,POL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ size_t
__TEMPLATE_CLASS__::_get_set_index(uint64_t x)
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

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
