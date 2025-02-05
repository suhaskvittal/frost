/*
 *  author: Suhas Vittal
 *  date:   4 February 2025
 * */

#ifndef CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h
#define CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h

#include "cache.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<IMPL,NUM_SETS,NUM_WAYS,NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
class VirtualWriteQueue : public __TEMPLATE_PARENT__
{
public:
    constexpr static size_t VWQ_WAYS = NUM_WAYS / 4;
    constexpr static size_t VWQ_HIGH_WATERMARK = (NUM_SETS*VWQ_WAYS) / 2;
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
    using typename __TEMPLATE_PARENT__::cset_type;
    using typename __TEMPLATE_PARENT__::way_iterator;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    using typename __TEMPLATE_PARENT__::multi_fill_result_type;

    void tick(void) override;
    void channel_request_demand_writeback(size_t channel_id);
protected:
    bool probe(uint64_t, bool write=false) override;
    bool mark(uint64_t, bool dirty) override;
    multi_fill_result_type fill(uint64_t, size_t, bool) override;

    way_iterator find_dirty_way(cset_type&);
    size_t count_dirty_lines_in_vwq_ways(const cset_type&) const;

    void update_criticality_via_count(size_t idx);

    using __TEMPLATE_PARENT__::get_set;
    using __TEMPLATE_PARENT__::enqueue_writeback;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class>
struct is_virtual_write_queue : std::false_type {};

template <class IMPL, size_t NUM_SETS, size_t NUM_WAYS, class NEXT_TYPE>
struct is_virtual_write_queue<VirtualWriteQueue<IMPL, NUM_SETS, NUM_WAYS, NEXT_TYPE>> : std::true_type {};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "virtual_write_queue.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CACHE_OTHER_IMPL_VIRTUAL_WRITE_QUEUE_h
