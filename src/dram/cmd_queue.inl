/*
 *  author: Suhas Vittal
 *  date:   25 December 2024
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool
CommandScheduler::can_accept(uint64_t address, bool is_write)
{
#if defined(DRAM_ENABLE_BG_WRITE_SYNC)
    if (is_write)
    {
        size_t ii = get_bankgroup_idx(address);
        bool out = bg_write_queues_[ii].can_accept(true);
        if (!out)
        {
            bg_drain_idx_ = ii;
            bg_write_mode_ = true;
        }
        return out;
    }
#endif
    size_t ii = get_bank_idx(address);
    return per_bank_queues_[ii].can_accept(is_write);
}

inline bool
CommandScheduler::has_no_pending_reads() const
{
    return std::all_of(per_bank_queues_.begin(), per_bank_queues_.end(),
                [] (const auto& q) { return q.has_no_pending_reads(); });
}

inline void
CommandScheduler::enqueue(DRAMCommand&& cmd)
{
#if defined(DRAM_ENABLE_BG_WRITE_SYNC)
    if (cmd_is_write(cmd.type))
    {
        size_t ii = get_bankgroup_idx(cmd.trans.address);
        bg_write_queues_[ii].enqueue(std::move(cmd));
        if (!bg_write_mode_ && bg_write_queues_[ii].size() == CommandScheduler::BG_WRITE_QUEUE_SIZE)
        {
            bg_drain_idx_ = ii;
            bg_write_mode_ = true;
        }
        return;
    }
#endif
    size_t ii = get_bank_idx(cmd.trans.address);
    per_bank_queues_[ii].enqueue(std::move(cmd));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline size_t
get_bankgroup_idx(uint64_t address)
{
    return dram_bankgroup(address) + dram_rank(address)*DRAM_BANKGROUPS;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
