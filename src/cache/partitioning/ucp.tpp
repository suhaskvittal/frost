/*
 *  author: Suhas Vittal
 *  date:   19 February 2025
 * */

#include <algorithm>
#include <cstring>
#include <iomanip>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_HEADER__ template <class IMPL>
#define __UMON_TEMPLATE__ UMON<IMPL>
#define __UCP_TEMPLATE__ UCPManager<IMPL>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__
__UMON_TEMPLATE__::UMON()
    :hit_counters(IMPL::NUM_WAYS, 0)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__UMON_TEMPLATE__::atd_probe(const Transaction& trans)
{
    auto s_it = atd_set_lookup(trans);
    if (s_it == atd.csets.end())
        return;

    cset_type& s = *s_it;

    auto it = cset_find(trans.address, s.begin(), s.end()); 
    if (it != s.end())
    {
        // line has been found -- compute the **MRU** position of the line and update the hit counter
        size_t p = IMPL::NUM_WAYS - cset_get_lru_position_of_entry(*it, s.begin(), s.end()) - 1;
        ++hit_counters[p];

        // update the entry:
        it->timestamp = GL_CYCLE;
    }
    else
    {
        ++total_misses;
        atd_fill(trans);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__UMON_TEMPLATE__::atd_fill(const Transaction& trans)
{
    auto s_it = atd_set_lookup(trans);
    if (s_it == atd.csets.end())
        return;

    cset_type& s = *s_it;

    auto v_it = std::find_if_not(s.begin(), s.end(),
                            [] (const auto& e) { return e.valid; });
    if (v_it == s.end())
    {
        // Search for victim via LRU:
        v_it = std::min_element(s.begin(), s.end(),
                            [] (const auto& x, const auto& y) { return x.timestamp < y.timestamp; });
    }
    
    // Update way:
    v_it->valid = true;
    v_it->address = trans.address;
    v_it->timestamp = GL_CYCLE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ cset_array::iterator
__UMON_TEMPLATE__::atd_set_lookup(const Transaction& trans)
{
    size_t idx = cache_set_index<IMPL>(trans.address);

    if (fast_mod<SET_MODULUS>(idx) != 0)
        return atd.csets.end();

    idx = idx >> numeric_traits<SET_MODULUS>::log2;
    if (idx >= atd.csets.size())
        exit(1);
    return atd.csets.begin() + idx;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define UCP_ENABLE_LOGGER

__TEMPLATE_HEADER__
__UCP_TEMPLATE__::UCPManager()
#if defined(UCP_ENABLE_LOGGER)
    :ucp_logger_("ucp.log")
#endif
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

__TEMPLATE_HEADER__ void
__UCP_TEMPLATE__::update_partition(part_iterator begin, part_iterator end)
{
    if constexpr (NUM_THREADS == 1)
        return;  // Don't need to do anything

#if defined(UCP_ENABLE_LOGGER)
    ucp_logger_ << "\n===============================================\n"
                << "UCP @ CYCLE = " << GL_CYCLE << " | CORE COUNT = " << NUM_THREADS << "\n"
                << std::setw(8) << std::left << "UMON#" << std::setw(8) << "MISSES";
    for (size_t i = 0; i < IMPL::NUM_WAYS; i++)
        ucp_logger_ << std::setw(8) << ("HIT_" + std::to_string(i));

    for (size_t i = 0; i < NUM_THREADS; i++)
    {
        ucp_logger_ << "\n" << std::setw(8) << i << std::setw(8) << umon_[i].total_misses;
        for (size_t j = 0; j < IMPL::NUM_WAYS; j++)
            ucp_logger_ << std::setw(8) << umon_[i].hit_counters[j];
    }
#endif

    if constexpr (NUM_THREADS == 2)
    {
        // Can do optimal selection:
        size_t best_way = 0;
        size_t best_way_util = std::numeric_limits<size_t>::max();
        for (size_t i = 1; i < IMPL::NUM_WAYS; i++)
        {
            size_t u = umon_[0].utility(i) + umon_[1].utility(IMPL::NUM_WAYS-i);
            if (u < best_way_util)
            {
                best_way = i;
                best_way_util = u;
            }

#if defined(UCP_ENABLE_LOGGER)
            ucp_logger_ << "\n\tCORE 0 ALLOC W = " << i << "\tUTILITY = " << u;
#endif
        }
        // Core 0 gets `*way_it`, Core 1 gets everything else:
        *begin =     best_way;
        *(begin+1) = IMPL::NUM_WAYS - best_way;
    }
    else
    {
        // Lookahead algorithm:
        //
        // Define structure for lookahead data:
        struct lookahead_data_type
        {
            size_t marginal_util;
            size_t line_count;
        };

        // Initialize lookahead data structure:
        std::array<lookahead_data_type, NUM_THREADS> lookahead_array{};

        int remaining = IMPL::NUM_WAYS;

        // Initialize partitions to 0:
        std::fill(begin, end, 1);
        remaining -= std::distance(begin, end);

        [[maybe_unused]] int round_number = 0;
        while (remaining > 0)
        {
#if defined(UCP_ENABLE_LOGGER)
            ucp_logger_ << "\n\tLOOKAHEAD ROUND " << round_number << " ------\n";
            ucp_logger_ << std::setw(48) << std::left << "";
            for (size_t ii = 1; ii <= remaining; ii++)
            {
                std::string label = "ALLOC = " + std::to_string(ii);
                ucp_logger_ << std::setw(12) << label;
            }
#endif
            // Unfortunately, gotta compute the best update the old-fashioned way:
            ssize_t i_max = -1;
            for (size_t i = 0; i < NUM_THREADS; i++)
            {
                auto it = begin+i;

                auto& e = lookahead_array[i];
                memset(&e, 0, sizeof(lookahead_data_type));

#if defined(UCP_ENABLE_LOGGER)
                std::string header = "\n\t\tCORE " + std::to_string(i) + " MU (CURR_ALLOC = " + std::to_string(*it) + "):";
                ucp_logger_ << std::setw(48) << std::left << header;
#endif

                // Compute `marginal_util`
                for (size_t ii = 1; ii <= remaining; ii++)
                {
                    size_t a = *it,
                           b = *it + ii;
                    size_t mu = umon_[i].utility_difference(a, b) / (b-a);
                    if (mu > e.marginal_util)
                    {
                        e.marginal_util = mu;
                        e.line_count = ii;
                    }
#if defined(UCP_ENABLE_LOGGER)
                    ucp_logger_ << std::setw(12) << mu;
#endif
                }

                // Update max:
                if (i_max < 0 || e.marginal_util > lookahead_array[i_max].marginal_util)
                    i_max = i;
            }

            // Update `*it` and `remaining`:
            auto& m = lookahead_array[i_max];
            if (m.line_count == 0)
            {
                break;
            }
            remaining -= m.line_count;
            *(begin+i_max) += m.line_count;

            ++round_number;

#if defined(UCP_ENABLE_LOGGER)
            ucp_logger_ << "\n\t\tCORE " << i_max << " WAS GIVEN " << m.line_count 
                << " LINES, MU = " << m.marginal_util;
#endif
        }

        size_t ii = 0;
        while (remaining--)
        {
            *(begin+ii) += 1;
            fast_increment_and_mod_inplace<NUM_THREADS>(ii);
        }
    }

    // Update last update cycle and counters:
    last_update_cycle_ = GL_CYCLE;
    for (auto& u : umon_)
    {
        // Divide counters by 2:
        for (auto& c : u.hit_counters)
            c >>= 1;
        u.total_misses >>= 1;
    }

#if defined(UCP_ENABLE_LOGGER)
    ucp_logger_ << "\nFINAL ALLOCATIONS:";
    for (auto it = begin; it != end; it++)
        ucp_logger_ << " " << *it;
    ucp_logger_ << "\n===============================================\n";
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_HEADER__
#undef __UMON_TEMPLATE__
#undef __UCP_TEMPLATE__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
