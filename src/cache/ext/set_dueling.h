/*
 *  author: Suhas Vittal
 *  date:   19 February 2025
 * */

#ifndef CACHE_EXT_SET_DUELING_h
#define CACHE_EXT_SET_DUELING_h

#include <cstdint>
#include <cstddef>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Implementation: we use a 11-bit PSEL with 64 leader sets for each replacement policy.
 *
 * The replacement policies used for set dueling depend on the replacement policy. For example, DRRIP
 * will use SRRIP and BRRIP and modify `init_entry` (see below, implementation is in `replacement.tpp`).
 * */

struct SetDuelingMonitor
{
    /*
     * We have marked each enum-type with an integer that corresponds to a psel
     * update on a miss.
     * */
    enum class Role 
    {
        FOLLOWER =0,
        LEADER_1 =1,
        LEADER_2 =-1
    };

    using psel_type = int16_t;

    constexpr static size_t    LEADER_SETS = 64;
    constexpr static size_t    PSEL_WIDTH = 11;
    constexpr static psel_type PSEL_DEFAULT = (1<<(PSEL_WIDTH-1))-1;
    constexpr static psel_type PSEL_MSB_MASK = 1 << (PSEL_WIDTH-1);

    psel_type psel =PSEL_DEFAULT;
    /*
     * `bimodal_counter`: for BRRIP (used in DRRIP)
     * */
    size_t    bimodal_counter =0;

    // Functions:
    Role get_role_of_set(size_t idx) const;
    void update_psel(size_t idx);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_EXT_SET_DUELING_h
