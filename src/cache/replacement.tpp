/*
 *  author: Suhas Vittal
 *  date:   31 January 2025
 * */

#include <cstdlib>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::update_entry(CacheEntry& e)
{
    e.timestamp = GL_CYCLE;
    e.rrpv = RRIP_MAX;
}

__TEMPLATE_HEADER__ void
__TEMPLATE_CLASS__::init_entry(CacheEntry& e, const Transaction& trans)
{
    e.coreid = trans.coreid;
    e.valid = true;
    e.dirty = trans.is_write();
    e.address = trans.address;
    e.timestamp = GL_CYCLE;
    e.likely_dead = dead_block_pred_->predict_if_dead(trans);

    if constexpr (IMPL::REPL == CacheReplPolicy::DRRIP)
    {
        // Check whether or not to use BRRIP.
        size_t idx = cache_set_index<IMPL>(trans.address);
        SetDuelingMonitor::Role r = set_dueling_arbiter_.get_role_of_set(idx);
        // Resolve `r` if it is a follower set.
        if (r == SetDuelingMonitor::Role::FOLLOWER)
        {
            r = (set_dueling_arbiter_.psel & SetDuelingMonitor::PSEL_MSB_MASK) 
                                ? SetDuelingMonitor::Role::LEADER_2 : SetDuelingMonitor::Role::LEADER_1;
        }

        if (r == SetDuelingMonitor::Role::LEADER_1)
        {
            e.rrpv = 1;
            ++s_dueling_pol1_installs_;
        }
        else
        {
            e.rrpv = (set_dueling_arbiter_.bimodal_counter == 32) ? 1 : 0;
            ++s_dueling_pol2_installs_;
            fast_increment_and_mod_inplace<32>(set_dueling_arbiter_.bimodal_counter);
        }
    }
    else
    {
        e.rrpv = 1;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_lru(cset_type& s, const Transaction& trans)
{
    // Check if we need to deal with partitioning:
    if constexpr (std::is_same<typename IMPL::PARTITION_MANAGER_TYPE, NoPartitionManager>::value)
    {
        return cset_get_way_in_lru_position(s.begin(), s.end(), 0);
    }
    else
    {
        // So, we need to select carefully, first compute the number of ways currently 
        // belonging to the incoming line.
        size_t cnt = std::count_if(s.begin(), s.end(),
                                [c=trans.coreid] (const auto& e) { return e.coreid == c; });

        // Determine if we need to evict from other cores or not
        bool match_coreid = (cnt >= partition_[trans.coreid]);
        if (match_coreid)
        {
            return std::min_element(s.begin(), s.end(),
                                [c=trans.coreid] (const auto& x, const auto& y)
                                {
                                    bool x_match = (x.coreid == c),
                                         y_match = (y.coreid == c);

                                    if (x_match == y_match)
                                        return x.timestamp < y.timestamp;
                                    else
                                        return x_match;
                                });
                                
        }
        else
        {
            // Need to determine which cores have used their entire budget:
            std::vector<bool> ok_to_evict(NUM_THREADS, false);
            for (uint8_t c = 0; c < NUM_THREADS; c++)
            {
                size_t ccnt = std::count_if(s.begin(), s.end(),
                                        [c] (const auto& e) { return e.coreid == c; });
                ok_to_evict[c] = (ccnt >= partition_[c]);
            }

            return std::min_element(s.begin(), s.end(),
                                [&ok_to_evict] (const auto& x, const auto& y)
                                {
                                    bool x_ok = ok_to_evict[x.coreid],
                                         y_ok = ok_to_evict[y.coreid];

                                    if (x_ok == y_ok)
                                        return x.timestamp < y.timestamp;
                                    else
                                        return x_ok;
                                });
        }
    }
}

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_rand(cset_type& s, const Transaction&)
{
    return std::next(s.begin(), fast_mod<IMPL::NUM_WAYS>(std::rand()));
}

__TEMPLATE_HEADER__ inline typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_rrip(cset_type& s, const Transaction&)
{
    auto v_it = std::min_element(s.begin(), s.end(),
                        [] (const auto& x, const auto& y) { return x.rrpv < y.rrpv; });
    auto r = v_it->rrpv;
    for (auto& x : s)
        x.rrpv -= r;
    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ typename __TEMPLATE_CLASS__::way_iterator
__TEMPLATE_CLASS__::repl_lru_dead_block(cset_type& s, const Transaction& trans)
{
    auto v_it = s.end();

    // Perform a prediction on the fill address:
    bool fill_is_likely_dead = dead_block_pred_->predict_if_dead(trans);
    if (fill_is_likely_dead)  // if so, do bypass:
        return v_it;
    
    // Otherwise, check for a dead block:
    v_it = std::find_if(s.begin(), s.end(), 
                [] (const auto& e) { return e.likely_dead; });

    // If no dead block exists, use LRU:
    if (v_it == s.end())
        v_it = repl_lru(s, trans);

    return v_it;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
