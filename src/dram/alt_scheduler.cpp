/*
 *  author: Suhas Vittal
 *  date:   26 February 2025
 * */

#include "dram/channel.h"
#include "dram/alt_scheduler.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

AlternateDRAMScheduler::AlternateDRAMScheduler(DRAMChannel* c, const DRAMChannelState& s)
    :owning_channel_(c),
    channel_state_(s),
    low_watermark_(DRAM_WQ_SIZE * DRAM_QUEUE_COUNT * OPT_DRAM_LOW_WATERMARK),
    high_watermark_(DRAM_WQ_SIZE * DRAM_QUEUE_COUNT * OPT_DRAM_HIGH_WATERMARK)
{
    read_queue_.reserve(DRAM_RQ_SIZE);
    write_queue_.reserve(DRAM_WQ_SIZE);

    for (auto& q : cmd_queues_)
        q.reserve(32);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
AlternateDRAMScheduler::add_incoming(Transaction trans)
{
    if (pending_writes_.find(trans.address) != pending_writes_.end())
    {
        if (trans.is_read())
            owning_channel_->outgoing_queue_.emplace(trans, GL_DRAM_CYCLE+1);

        ++s_write_forwards_;
        return true;
    }

    auto& q =       trans.is_read() ? read_queue_       : write_queue_;
    auto& p =       trans.is_read() ? pending_reads_    : pending_writes_;
    size_t s =      trans.is_read() ? DRAM_RQ_SIZE      : DRAM_WQ_SIZE;

    if (q.size() < s)
    {
        p.insert(trans.address);
        q.push_back(trans);
        return true;
    }
    
    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

AlternateDRAMScheduler::cmd_output_type
AlternateDRAMScheduler::select_ready_command()
{
    for (size_t i = 0; i < DRAM_TOT_BANKS_PER_CHANNEL; i++)
    {
        auto& q = cmd_queues_[next_bank_idx_];

        const auto& b = channel_get_const_bank_ref_from_idx(channel_state_, next_bank_idx_);
        bool is_first = true;

        fast_increment_and_mod_inplace(next_bank_idx_, DRAM_TOT_BANKS_PER_CHANNEL);

        for (auto q_it = q.begin(); q_it != q.end(); q_it++)
        {
            // Enforce RW dependency
            if (q_it->trans.is_write() && pending_reads_.find(q_it->trans.address) != pending_reads_.end())
                continue;

            DRAMCommand ready_cmd;
            ready_cmd.address = q_it->trans.address;

            if (b.open_row.has_value())
            {
                size_t row = dram_row(q_it->trans.address);
                if (b.open_row == row)
                {
                    ready_cmd.type = q_it->trans.is_read() ? DRAMCommand::Type::READ : DRAMCommand::Type::WRITE;
                    ready_cmd.autopre = (DRAM_PAGE_POLICY == DRAMPagePolicy::CLOSE);
                }
                else if (is_first)
                {
                    bool any_pending_hits = std::any_of(std::next(q_it), q.end(),
                                                [row=b.open_row.value()] (const auto& e)
                                                {
                                                    return dram_row(e.trans.address) == row;
                                                });
                    if (!any_pending_hits || (b.num_cas_to_open_row >= 4))
                        ready_cmd.type = DRAMCommand::Type::PRECHARGE;
                }
            }
            else
            {
                ready_cmd.type = DRAMCommand::Type::ACTIVATE;
            }

            bool cmd_ok = !ready_cmd.is_invalid() && cmd_is_issuable(channel_state_, ready_cmd);

            if (cmd_ok)
            {
                std::optional<RWQueueEntry> q_entry;
                if (ready_cmd.is_cas())
                {
                    q_it->is_row_buffer_hit = b.next_cas_is_row_buffer_hit;
                    q_entry.emplace(std::move(*q_it));
                    q.erase(q_it);

                    auto& p = ready_cmd.is_read() ? pending_reads_ : pending_writes_;
                    p.erase(p.find(ready_cmd.address));

                    if (p.find(ready_cmd.address) != p.end())
                    {
                        auto it = std::remove_if(q.begin(), q.end(),
                                            [addr=ready_cmd.address]
                                            (const auto& e) { return e.trans.address == addr; });
                        q.erase(it, q.end());

                        auto& tq = ready_cmd.is_read() ? read_queue_ : write_queue_;
                        it = std::remove_if(tq.begin(), tq.end(),
                                            [addr=ready_cmd.address]
                                            (const auto& e) { return e.trans.address == addr; });
                        tq.erase(it, tq.end());
                        
                        p.erase(ready_cmd.address);
                    }
                }

                return cmd_output_type{ready_cmd, q_entry};
            }
        }
    }

    return cmd_output_type{};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
AlternateDRAMScheduler::update_state()
{
    bool drain_cond_1 = write_queue_.size() == DRAM_WQ_SIZE;
    bool drain_cond_2 = std::all_of(cmd_queues_.begin(), cmd_queues_.end(),
                                [] (const auto& q) { return q.empty(); }) && write_queue_.size() > 8;

    if (write_draining_ == 0 && (drain_cond_1 || drain_cond_2))
        write_draining_ = write_queue_.size();

    auto& q = write_draining_ > 0 ? write_queue_ : read_queue_;
    auto q_it = std::find_if(q.begin(), q.end(),
                        [this, write_mode=write_draining_>0] (const auto& e)
                        {
                            if (write_mode && pending_reads_.count(e.trans.address))
                                return false;
                            size_t bank_idx = dram_bank_idx(e.trans.address);
                            return this->cmd_queues_[bank_idx].size() < 32;
                        });
    if (q_it != q.end())
    {
        if (q_it->trans.is_write())
            --write_draining_;

        size_t bank_idx = dram_bank_idx(q_it->trans.address);
        cmd_queues_[bank_idx].push_back(std::move(*q_it));
        q.erase(q_it);
    }
    else
    {
        write_draining_ = 0;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
