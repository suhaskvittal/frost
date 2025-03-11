/*
 *  author: Suhas Vittal
 *  date:   4 February 2025
 * */

#ifndef CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h
#define CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h

#include "cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<IMPL,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern int OPT_VWQ_WAYS;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, class NEXT_TYPE>
class VirtualWriteQueue : public __TEMPLATE_PARENT__
{
public:
    using __TEMPLATE_PARENT__::s_writebacks_;
    using __TEMPLATE_PARENT__::s_eager_writebacks_;
private:
    using critical_map_type = std::unordered_map<size_t, size_t>;
    using way_counter_array = std::vector<ssize_t>;
    /*
     * `critical_map_` counts the number of dirty virtual-write-queue ways.
     * */
    critical_map_type           critical_map_;
    critical_map_type::iterator drain_next_it_;
    critical_map_type::iterator demand_next_it_;

    way_counter_array write_hits_;
    
    size_t queue_size_ =0;
    bool in_write_mode_ =false;

    ssize_t num_vwq_ways_;

    using __TEMPLATE_PARENT__::writeback_queue_;
    using __TEMPLATE_PARENT__::csets_;
public:
    using typename __TEMPLATE_PARENT__::way_iterator;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    using typename __TEMPLATE_PARENT__::multi_fill_result_type;

    VirtualWriteQueue(std::string cache_name, typename __TEMPLATE_PARENT__::next_ptr& n)
        :__TEMPLATE_PARENT__(cache_name, n),
        write_hits_(IMPL::NUM_WAYS, 0),
        num_vwq_ways_(OPT_VWQ_WAYS < 0 ? 4 : OPT_VWQ_WAYS),
        demand_next_it_(critical_map_.end())
    {}

    void tick(void) override;
    void channel_request_demand_writeback(size_t channel_id);
protected:
    bool probe(const Transaction&) override;
    bool mark_dirty(const Transaction&) override;
    multi_fill_result_type fill(const Transaction&) override;

    void child_handle_mark_dirty_hit(cset_type&, cset_type::iterator, const Transaction&) override;

    void enqueue_writeback(Transaction) override;

    way_iterator find_dirty_way(cset_type&);
    size_t count_dirty_lines_in_vwq_ways(const cset_type&) const;

    void update_criticality_via_count(size_t idx);

    inline size_t high_watermark(void) const
    {
        return 0.75 * IMPL::NUM_SETS * num_vwq_ways_;
    }

    inline size_t low_watermark(void) const
    {
        return 0.25 * IMPL::NUM_SETS * num_vwq_ways_;
    }

    using __TEMPLATE_PARENT__::mshr_has_space;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "virtual_write_queue.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h
