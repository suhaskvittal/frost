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
        if (!bg_write_mode_ && !out)
            bg_sync_init(ii);
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
        return;
    }
#endif
    size_t ii = get_bank_idx(cmd.trans.address);
    per_bank_queues_[ii].enqueue(std::move(cmd));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
CommandScheduler::bg_sync_init(size_t start)
{
    bg_drain_idx_ = start;
    bg_write_mode_ = true;
    bg_num_writes_ = 0;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline const DRAMBankState&
get_bank_state(const DRAMChannelState& ch, uint64_t address)
{
    size_t ra = dram_rank(address),
           bg = dram_bankgroup(address),
           ba = dram_bank(address);
    return ch.at(ra).at(bg).at(ba);
}

inline size_t
get_bankgroup_idx(uint64_t address)
{
    return dram_bankgroup(address) + dram_rank(address)*DRAM_BANKGROUPS;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
