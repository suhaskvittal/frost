/*
 *  author: Suhas Vittal
 *  date:   25 December 2024
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool
CommandScheduler::can_accept(uint64_t address, bool is_write) const
{
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
    size_t ii = get_bank_idx(cmd.trans.address);
    return per_bank_queues_[ii].enqueue(std::move(cmd));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
