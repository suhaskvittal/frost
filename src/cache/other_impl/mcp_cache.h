/*
 *  author: Suhas Vittal
 *  date:   26 Februaryu 2025
 * */

#ifndef CACHE_OTHER_IMPL_MCP_CACHE_h
#define CACHE_OTHER_IMPL_MCP_CACHE_h

#include "cache.h"
#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define __TEMPLATE_PARENT__ Cache<IMPL, NEXT_TYPE>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class IMPL, class NEXT_TYPE>
class MCPCache : public __TEMPLATE_PARENT__
{
public:
    using __TEMPLATE_PARENT__::s_writebacks_;
    using __TEMPLATE_PARENT__::s_eager_writebacks_;
protected:
    using __TEMPLATE_PARENT__::csets_;
    using __TEMPLATE_PARENT__::partition_manager_;
private:
    using channel_bitvec_type = std::array<bool, DRAM_CHANNELS>;

    channel_bitvec_type write_mode_{};
public:
    using __TEMPLATE_PARENT__::Cache;
    using typename __TEMPLATE_PARENT__::way_iterator;
    using typename __TEMPLATE_PARENT__::fill_result_type;
    using typename __TEMPLATE_PARENT__::multi_fill_result_type;

    inline void toggle_write_mode(size_t channel_id, bool w)
    {
        write_mode_[channel_id] = w;
        mcp_manager()->toggle_write_mode(channel_id, w);
    }

    void channel_enter_write_mode(size_t channel_id);
    void channel_exit_write_mode(size_t channel_id);
protected:
    multi_fill_result_type fill(const Transaction&) override;

    way_iterator find_victim(size_t set_index, cset_type&, const Transaction&) override;
    way_iterator repl_lru_mcp(size_t set_index, cset_type&, const Transaction&);

    inline MinimalistPartitionManager<IMPL>* mcp_manager(void)
    {
        if constexpr (!std::is_same<typename IMPL::PARTITION_MANAGER_TYPE, MinimalistPartitionManager<IMPL>>::value)
        {
            std::cerr << "[ MCPCache::mcp_manager ] partition manager type is not `MinimalistPartitionManager`\n";
            exit(1);
        }
        return static_cast<MinimalistPartitionManager<IMPL>*>(partition_manager_.get());
    }

    using __TEMPLATE_PARENT__::enqueue_writeback;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "mcp_cache.tpp"

#undef __TEMPLATE_PARENT__

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif // CACHE_OTHER_IMPL_MCP_CACHE_h
