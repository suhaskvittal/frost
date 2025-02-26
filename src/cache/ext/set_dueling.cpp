/*
 *  author: Suhas Vittal
 *  date:   19 February 2025
 * */

#include "cache/ext/set_dueling.h"
#include "util/numerics.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

SetDuelingMonitor::Role
SetDuelingMonitor::get_role_of_set(size_t idx) const
{
    size_t grp = idx >> ilog2(LEADER_SETS),
           off = fast_mod(idx, LEADER_SETS);
    size_t coff = off ^ (LEADER_SETS-1);

    if (grp == off)
        return Role::LEADER_1;
    else if (grp == coff)
        return Role::LEADER_2;
    else
        return Role::FOLLOWER;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
SetDuelingMonitor::update_psel(size_t idx)
{
    constexpr static psel_type PSEL_MAX = (1<<PSEL_WIDTH)-1;
    constexpr static psel_type PSEL_MIN = 0;
    
    Role r = get_role_of_set(idx);
    psel += static_cast<psel_type>(r);
    psel = std::clamp(psel, PSEL_MIN, PSEL_MAX);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
