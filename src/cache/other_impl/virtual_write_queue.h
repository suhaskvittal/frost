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

template <class IMPL, class NEXT_TYPE>
class VirtualWriteQueue : public __TEMPLATE_PARENT__
{
public:
    constexpr static size_t VWQ_WAYS = IMPL::NUM_WAYS / 4;
    constexpr static size_t VWQ_HIGH_WATERMARK = (IMPL::NUM_SETS*VWQ_WAYS) / 2;
    constexpr static size_t VWQ_LOW_WATERMARK = VWQ_HIGH_WATERMARK - DRAM_WQ_SIZE;

    using __TEMPLATE_PARENT__::s_writebacks_;
    using __TEMPLATE_PARENT__::s_eager_writebacks_;
private:
    using critical_map_type = std::unordered_map<size_t, size_t>;
    /*
     * `critical_map_` counts the number of dirty virtual-write-queue ways.
     * */
    critical_map_type           critical_map_;
    critical_map_type::iterator next_it_;
    
    size_t queue_size_ =0;
    bool in_write_mode_ =false;

    using __TEMPLATE_PARENT__::writeback_queue_;
    using __TEMPLATE_PARENT__::csets_;
public:
    using __TEMPLATE_PARENT__::Cache;
    using typename __TEMPLATE_PARENT__::way_iterator;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    using typename __TEMPLATE_PARENT__::multi_fill_result_type;

    void tick(void) override;
    void channel_request_demand_writeback(size_t channel_id);
protected:
    bool probe(const Transaction&) override;
    bool mark_dirty(const Transaction&) override;
    multi_fill_result_type fill(const Transaction&) override;

    way_iterator find_dirty_way(cset_type&);
    size_t count_dirty_lines_in_vwq_ways(const cset_type&) const;

    void update_criticality_via_count(size_t idx);

    using __TEMPLATE_PARENT__::enqueue_writeback;
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
