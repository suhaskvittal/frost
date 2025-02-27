/*
 *  author: Suhas Vittal
 *  date:   24 February 2025
 * */


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL, size_t NUM_SETS>
#define __TEMPLATE_CLASS__ AuxTagDirectory<IMPL, NUM_SETS>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
template <class CALLBACK_TYPE> ATDLookupResult
__TEMPLATE_CLASS__::probe(const Transaction& trans, const CALLBACK_TYPE& callback)
{
    auto s_it = set_lookup(trans);
    if (s_it == csets.end())
        return ATDLookupResult::IGNORED;

    cset_type& s = *s_it;

    // Do probe:
    auto it = cset_find(trans.address, s.begin(), s.end());
    ATDLookupResult r = (it == s.end()) ? ATDLookupResult::MISS : ATDLookupResult::HIT;

    callback(s, it);

    // Update on hit:
    if (it != s.end())
        it->timestamp = GL_CYCLE;

    return r;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
template <class CALLBACK_TYPE> ATDLookupResult
__TEMPLATE_CLASS__::mark_dirty(const Transaction& trans, const CALLBACK_TYPE& callback)
{
    auto s_it = set_lookup(trans);
    if (s_it == csets.end())
        return ATDLookupResult::IGNORED;

    cset_type& s = *s_it;

    // Search for entry:
    auto it = cset_find(trans.address, s.begin(), s.end());
    ATDLookupResult r = (it == s.end()) ? ATDLookupResult::MISS : ATDLookupResult::HIT;

    callback(s, it);

    if (it != s.end())
        it->dirty = true;

    return r;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
template <class CALLBACK_TYPE> CacheEntry
__TEMPLATE_CLASS__::fill(const Transaction& trans, const CALLBACK_TYPE& callback)
{
    CacheEntry out;

    auto s_it = set_lookup(trans);
    if (s_it == csets.end())
        return out;

    cset_type& s = *s_it;

    if (cset_find(trans.address, s.begin(), s.end()) != s.end())
        return out;

    // Check for invalid entries:
    auto it = std::find_if_not(s.begin(), s.end(),
                        [] (const auto& e) { return e.valid; });
    if (it == s.end())
    {
        // Compute LRU victim:
        it = std::min_element(s.begin(), s.end(),
                        [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
        out = std::move(*it);
    }

    it->valid = true;
    it->address = trans.address;
    it->timestamp = GL_CYCLE;
    it->dirty = trans.is_write();

    callback(s, out);

    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ cset_array::iterator
__TEMPLATE_CLASS__::set_lookup(const Transaction& trans)
{
    size_t base_idx = cache_set_index<IMPL>(trans.address);

    if (fast_mod(base_idx, SET_MODULUS) != 0)
        return csets.end();
    else
        return csets.begin() + (base_idx >> ilog2(SET_MODULUS));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __TEMPLATE_CLASS__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
