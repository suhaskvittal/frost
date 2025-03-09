/*
 *  author: Suhas Vittal
 *  date:   8 March 2025
 * */

#include "cache/other_impl/w_cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
repl_cmp_w(bool x_is_older, bool x_d, bool y_d, bool x_p, bool y_p)
{
    /*
     * Result matrix:
     *  ------------------------- x dirty && y dirty ---------------------------------
     *      x_p    y_p    out
     *       n      n     age
     *       y      n      x
     *       n      y      y
     *       y      y     age
     *  ------------------------- x dirty && y clean ---------------------------------
     *      x_p    y_p       out
     *       n      n         x
     *       y      n         x
     *       n      y        age
     *       y      y         x
     *  ------------------------- x clean && y dirty ---------------------------------
     *      x_p    y_p       out
     *       n      n         y
     *       y      n        age
     *       n      y         y
     *       y      y         y
     * */
    if (x_d && y_d)
        return ((x_p == y_p) && x_is_older) || ((x_p != y_p) && x_p);
    else if (x_d)
        return x_p || !y_p || x_is_older;
    else if (y_d)
        return x_p && !y_p && x_is_older;
    else
        return x_is_older;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

size_t
catch_and_release_select_pos(const std::vector<ssize_t>& ctrs)
{
    auto max_pos_it = std::find_if(ctrs.begin(), ctrs.end(),
                        [] (auto x) { return x >= 0; });

    bool no_nonzero_after = std::none_of(max_pos_it, ctrs.end(),
                                [] (auto x) { return x > 0; });
    if (no_nonzero_after)
        max_pos_it = ctrs.end();

    return std::distance(ctrs.begin(), max_pos_it);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
