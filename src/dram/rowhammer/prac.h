/*
 *  author: Suhas Vittal
 *  date:   22 February 2025
 * */

#ifndef DRAM_ROWHAMMER_PRAC_h
#define DRAM_ROWHAMMER_PRAC_h

#include "constants.h"

#include <array>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <bool SYNC_ACROSS_CHIP=true>
struct PRAC
{
public:
    constexpr size_t DRAM_CHIPS = 4;
    /*
     * Depending on what our implementation is (`SYNC_ACROSS_CHIP`) -- we will use a different
     * counter array:
     * */
    using ctr_type = int16_t;
    using ctr_array = std::conditional<SYNC_ACROSS_CHIP, unified_ctr_array, split_ctr_array>::type;

    ctr_array ctrs{};
    /*
     * Any additional metadata:
     * */
    struct
    {
        using tracked_row_type = std::optional<size_t>;

        size_t eligi_threshold;
        size_t alert_threshold;

        tracked_row_type row;

        size_t mitig_trefi_counter =0;
    } moat;
private:
    using unified_ctr_array = std::array<ctr_type, DRAM_ROWS>;
    using split_ctr_array = std::array<unified_ctr_array, DRAM_CHIPS>;
public:
    PRAC(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_ROWHAMMER_PRAC_h
