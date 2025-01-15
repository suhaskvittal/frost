#include "channel_state.h"

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

namespace dramsim3 {

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

ChannelState::ChannelState(const Config& config, const Timing& timing, SimpleStats& simple_stats)
    : rank_idle_cycles(config.ranks, 0),
      config_(config),
      timing_(timing),
      simple_stats_(simple_stats),
      rank_is_sref_(config.ranks, false),
      four_aw_(config_.ranks, std::vector<uint64_t>()),
      thirty_two_aw_(config_.ranks, std::vector<uint64_t>()) 
{
    
    bank_states_.reserve(config_.ranks);

    for (auto i = 0; i < config_.ranks; i++)
    {
        auto rank_states = std::vector<std::vector<BankState>>();
        rank_states.reserve(config_.bankgroups);
        for (auto j = 0; j < config_.bankgroups; j++)
        {
            std::vector<BankState> bg_states;
            for (auto k = 0; k < config_.banks_per_group; k++)
            {
                bg_states.push_back(BankState(config_, simple_stats_, i, j, k));
            }
            rank_states.push_back(bg_states);
        }
        bank_states_.push_back(rank_states);
    }
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::PrintDeadlock() const {
    for (auto i = 0; i < config_.ranks; i++) {
        for (auto j = 0; j < config_.bankgroups; j++) {
            for (auto k = 0; k < config_.banks_per_group; k++) {
                printf("Rank %d Bankgroup %d Bank %d\n", i, j, k);
                bank_states_[i][j][k].PrintState();
                printf("====================================\n");
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

bool ChannelState::IsAllBankIdleInRank(int rank) const {
    for (int j = 0; j < config_.bankgroups; j++) {
        for (int k = 0; k < config_.banks_per_group; k++) {
            if (bank_states_[rank][j][k].IsRowOpen()) {
                return false;
            }
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

bool ChannelState::IsRWPendingOnRef(const Command& cmd) const {
    int rank = cmd.Rank();
    int bankgroup = cmd.Bankgroup();
    int bank = cmd.Bank();
    return (IsRowOpen(rank, bankgroup, bank) &&
            RowHitCount(rank, bankgroup, bank) == 0 &&
            bank_states_[rank][bankgroup][bank].OpenRow() == cmd.Row());
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::BankNeedRefresh(int rank, int bankgroup, int bank,
                                   bool need) 
{
    if (need) {
        Address addr = Address(-1, rank, bankgroup, bank, -1, -1);
        refresh_q_.emplace_back(CommandType::REFRESH_BANK, addr, -1);
    } else {
        for (auto it = refresh_q_.begin(); it != refresh_q_.end(); it++) {
            if (it->Rank() == rank && it->Bankgroup() == bankgroup &&
                it->Bank() == bank) {
                refresh_q_.erase(it);
                break;
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::BanksetNeedRefresh(int rank, int bank, bool need) {
    if (need) {
        Address addr = Address(-1, rank, -1, bank, -1, -1);
        refresh_q_.emplace_back(CommandType::REFsb, addr, -1);
    } else {
        for (auto it = refresh_q_.begin(); it != refresh_q_.end(); it++) {
            if (it->Rank() == rank && it->Bank() == bank) {
                refresh_q_.erase(it);
                break;
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::RankNeedRefresh(int rank, bool need) {
    if (need) {
        Address addr = Address(-1, rank, -1, -1, -1, -1);
        refresh_q_.emplace_back(CommandType::REFab, addr, -1);
    } else {
        for (auto it = refresh_q_.begin(); it != refresh_q_.end(); it++) {
            if (it->Rank() == rank) {
                refresh_q_.erase(it);
                break;
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::BanksetNeedRFM(int rank, int bank, bool need) {
    // Check if the rank is already present in the queue
    bool bankset_present = false;
    for (const auto& cmd : rfm_q_)
    {
        if (cmd.Rank() == rank and cmd.Bank() == bank)
        {
            bankset_present = true;
            break;
        }
    }

    if (need and !bankset_present)
    {
        Address addr = Address(-1, rank, -1, bank, -1, -1);
        rfm_q_.emplace_back(CommandType::RFMsb, addr, -1);
    }
    else if (!need)
    {
        // Remove the rank from the queue if it is present
        for (auto it = rfm_q_.begin(); it != rfm_q_.end(); ++it) {
            if (it->Rank() == rank and bank == it->Bank()) {
                rfm_q_.erase(it);
                break;
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::RankNeedRFM(int rank, bool need) {
    // Check if the rank is already present in the queue
    bool rank_present = false;
    for (const auto& cmd : rfm_q_)
    {
        if (cmd.Rank() == rank)
        {
            rank_present = true;
            break;
        }
    }

    if (need and !rank_present)
    {
        Address addr = Address(-1, rank, -1, -1, -1, -1);
        rfm_q_.emplace_back(CommandType::RFMab, addr, -1);
    }
    else if (!need)
    {
        // Remove the rank from the queue if it is present
        for (auto it = rfm_q_.begin(); it != rfm_q_.end(); ++it) {
            if (it->Rank() == rank) {
                rfm_q_.erase(it);
                break;
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

Command ChannelState::GetReadyCommand(const Command& cmd, uint64_t clk) const {
    Command ready_cmd = Command();
    if (cmd.IsRankCMD()) {
        int num_ready = 0;
        for (auto j = 0; j < config_.bankgroups; j++) {
            for (auto k = 0; k < config_.banks_per_group; k++) {
                ready_cmd = bank_states_[cmd.Rank()][j][k].GetReadyCommand(cmd, clk);
                if (!ready_cmd.IsValid()) {  // Not ready
                    continue;
                }
                if (ready_cmd.cmd_type != cmd.cmd_type) {  // likely PRECHARGE
                    Address new_addr = Address(-1, cmd.Rank(), j, k, -1, -1);
                    ready_cmd.addr = new_addr;
                    return ready_cmd;
                } else {
                    num_ready++;
                }
            }
        }
        // All bank ready
        if (num_ready == config_.banks) {
            return ready_cmd;
        } else {
            return Command();
        }
    } 
    else if (cmd.IsSbCMD())
    {
        int num_ready = 0;
        for (auto j = 0; j < config_.bankgroups; j++) {
            ready_cmd = bank_states_[cmd.Rank()][j][cmd.Bank()].GetReadyCommand(cmd, clk);
            if (!ready_cmd.IsValid()) {  // Not ready
                continue;
            }
            if (ready_cmd.cmd_type != cmd.cmd_type) {  // likely PRECHARGE
                Address new_addr = Address(-1, cmd.Rank(), j, cmd.Bank(), -1, -1);
                ready_cmd.addr = new_addr;
                return ready_cmd;
            } else {
                num_ready++;
            }
        }
        // Same bank in all bankgroups ready
        if (num_ready == config_.bankgroups) {
            return ready_cmd;
        } else {
            return Command();
        }
    } 
    else
    {
        ready_cmd = bank_states_[cmd.Rank()][cmd.Bankgroup()][cmd.Bank()]
                        .GetReadyCommand(cmd, clk);
        if (!ready_cmd.IsValid()) return Command();

        // [ABO] allow 180ns of activity before 
        if (alert_n && clk > (config_.tABO + last_alert_clk_)) {
            // Calling a non-const function from a const function
            const_cast<ChannelState*>(this)->RankNeedRFM(ready_cmd.Rank(), true);
            const_cast<ChannelState*>(this)->ResetAlert();
            return Command();
        }
        
        // [RFM] Add to the RFM queue and return an empty command
        if (ready_cmd.cmd_type == CommandType::RFMsb)
        {
            // Calling a non-const function from a const function
            const_cast<ChannelState*>(this)->BanksetNeedRFM(ready_cmd.Rank(), ready_cmd.Bank(), true);
            return Command();    
        }

        if (ready_cmd.cmd_type == CommandType::RFMab)
        {
            // Calling a non-const function from a const function
            const_cast<ChannelState*>(this)->RankNeedRFM(ready_cmd.Rank(), true);
            return Command();    
        }

        if (ready_cmd.cmd_type == CommandType::ACTIVATE)
        {
            if (!ActivationWindowOk(ready_cmd.Rank(), clk))
            {
                return Command();
            }   
        }
        return ready_cmd;
    }
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

int get_channel(const Config& config_, uint64_t hex_addr)
{
    hex_addr >>= config_.shift_bits;
    return (hex_addr >> config_.ch_pos) & config_.ch_mask;
}

void
ChannelState::UpdateState(const Command& cmd, uint64_t clk) {
    if (cmd.IsRankCMD()) {
        for (auto j = 0; j < config_.bankgroups; j++) {
            for (auto k = 0; k < config_.banks_per_group; k++) {
                bank_states_[cmd.Rank()][j][k].UpdateState(cmd, clk);
            }
        }

        if (cmd.IsRFM()) {
            RankNeedRFM(cmd.Rank(), false);
            ResetAlert();
        } else if (cmd.IsRefresh()) {
            RankNeedRefresh(cmd.Rank(), false);
            ResetAlert();
        } else if (cmd.cmd_type == CommandType::SREF_ENTER) {
            rank_is_sref_[cmd.Rank()] = true;
        } else if (cmd.cmd_type == CommandType::SREF_EXIT) {
            rank_is_sref_[cmd.Rank()] = false;
        }
    } 
    else if (cmd.IsSbCMD())
    {
        for (auto j = 0; j < config_.bankgroups; j++) {
            bank_states_[cmd.Rank()][j][cmd.Bank()].UpdateState(cmd, clk);
        }
        if (cmd.IsRFM()) {
            BanksetNeedRFM(cmd.Rank(), cmd.Bank(), false);
        } else if (cmd.IsRefresh()) {
            BanksetNeedRefresh(cmd.Rank(), cmd.Bank(), false);
        }
    }
    else
    {
        bank_states_[cmd.Rank()][cmd.Bankgroup()][cmd.Bank()].UpdateState(cmd, clk);
        if (cmd.IsRefresh()) {
            BankNeedRefresh(cmd.Rank(), cmd.Bankgroup(), cmd.Bank(), false);
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void
ChannelState::TriggerSameBankAlert(const Command& cmd, uint64_t clk) {
    if (alert_n or num_acts_abo_ < config_.ABO_delay_acts) {
        return;
    }

    if (config_.alert_mode == 1 and bank_states_[cmd.Rank()][cmd.Bankgroup()][cmd.Bank()].CheckAlert()) {
        alert_n = true;
        last_alert_clk_ = clk;
        simple_stats_.Increment("num_alerts");
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::TriggerSameRankAlert(const Command& cmd, uint64_t clk) {
    if (alert_n or num_acts_abo_ < config_.ABO_delay_acts)
    {
        return;
    }

    for (auto j = 0; j < config_.bankgroups; j++) 
    {
        for (auto k = 0; k < config_.banks_per_group; k++)
        {
            if (alert_n)
            {
                return;
            }

            if (config_.alert_mode == 1 and bank_states_[cmd.Rank()][j][k].CheckAlert())
            {
                alert_n = true;
                last_alert_clk_ = clk;
                simple_stats_.Increment("num_alerts");
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateTiming(const Command& cmd, uint64_t clk) {
    switch (cmd.cmd_type) {
        case CommandType::ACTIVATE:
            num_acts_abo_++;
            UpdateActivationTimes(cmd.Rank(), clk);
        case CommandType::READ_PRECHARGE:
        case CommandType::READ_PRECHARGE2:
        case CommandType::WRITE_PRECHARGE:
        case CommandType::WRITE_PRECHARGE2:
        case CommandType::PRECHARGE:
        case CommandType::PRECHARGE2:
            TriggerSameBankAlert(cmd, clk);
        case CommandType::READ:
        case CommandType::WRITE:
        case CommandType::REFRESH_BANK:
            // TODO - simulator speed? - Speciazlize which of the below
            // functions to call depending on the command type  Same Bank
            UpdateSameBankTiming(
                cmd.addr, timing_.same_bank[static_cast<int>(cmd.cmd_type)],
                clk);

            // Same Bankgroup other banks
            UpdateOtherBanksSameBankgroupTiming(
                cmd.addr,
                timing_
                    .other_banks_same_bankgroup[static_cast<int>(cmd.cmd_type)],
                clk);

            // Other bankgroups
            UpdateOtherBankgroupsSameRankTiming(
                cmd.addr,
                timing_
                    .other_bankgroups_same_rank[static_cast<int>(cmd.cmd_type)],
                clk);

            // Other ranks
            UpdateOtherRanksTiming(
                cmd.addr, timing_.other_ranks[static_cast<int>(cmd.cmd_type)],
                clk);
            break;
        case CommandType::RFMab: // [RFM] All Bank RFM
            num_acts_abo_ = 0;
        case CommandType::REFab:
            TriggerSameRankAlert(cmd, clk);
        case CommandType::SREF_ENTER:
        case CommandType::SREF_EXIT:
            UpdateSameRankTiming(
                cmd.addr, timing_.same_rank[static_cast<int>(cmd.cmd_type)],
                clk);
            break;
        case CommandType::REFsb: // [RFM] Same Bank RFM
        case CommandType::RFMsb: // [RFM] Same Bank RFM
            TriggerSameRankAlert(cmd, clk);
            UpdateSameBankset(
                cmd.addr, timing_.same_bankset[static_cast<int>(cmd.cmd_type)],
                clk);
            
            UpdateOtherBanksets(
                cmd.addr, timing_.other_banksets[static_cast<int>(cmd.cmd_type)],
                clk);
            break;
        default:
            AbruptExit(__FILE__, __LINE__);
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateSameBankset(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) 
{
    for (auto j = 0; j < config_.bankgroups; j++) {
        for (auto cmd_timing : cmd_timing_list) {
            bank_states_[addr.rank][j][addr.bank].UpdateTiming(
                cmd_timing.first, clk + cmd_timing.second);
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateOtherBanksets(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto i = 0; i < config_.ranks; i++) {
        for (auto j = 0; j < config_.bankgroups; j++) {
            for (auto k = 0; k < config_.banks_per_group; k++) {
                if (k == addr.bank)
                {
                    continue;
                }
                for (auto cmd_timing : cmd_timing_list) {
                    bank_states_[i][j][k].UpdateTiming(
                        cmd_timing.first, clk + cmd_timing.second);
                }
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateSameBankTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) 
{
    for (auto cmd_timing : cmd_timing_list) {
        bank_states_[addr.rank][addr.bankgroup][addr.bank].UpdateTiming(
            cmd_timing.first, clk + cmd_timing.second);
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateOtherBanksSameBankgroupTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto k = 0; k < config_.banks_per_group; k++) {
        if (k != addr.bank) {
            for (auto cmd_timing : cmd_timing_list) {
                bank_states_[addr.rank][addr.bankgroup][k].UpdateTiming(
                    cmd_timing.first, clk + cmd_timing.second);
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateOtherBankgroupsSameRankTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto j = 0; j < config_.bankgroups; j++) {
        if (j != addr.bankgroup) {
            for (auto k = 0; k < config_.banks_per_group; k++) {
                for (auto cmd_timing : cmd_timing_list) {
                    bank_states_[addr.rank][j][k].UpdateTiming(
                        cmd_timing.first, clk + cmd_timing.second);
                }
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateOtherRanksTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto i = 0; i < config_.ranks; i++) {
        if (i != addr.rank) {
            for (auto j = 0; j < config_.bankgroups; j++) {
                for (auto k = 0; k < config_.banks_per_group; k++) {
                    for (auto cmd_timing : cmd_timing_list) {
                        bank_states_[i][j][k].UpdateTiming(
                            cmd_timing.first, clk + cmd_timing.second);
                    }
                }
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateSameRankTiming(
    const Address& addr,
    const std::vector<std::pair<CommandType, int>>& cmd_timing_list,
    uint64_t clk) {
    for (auto j = 0; j < config_.bankgroups; j++) {
        for (auto k = 0; k < config_.banks_per_group; k++) {
            for (auto cmd_timing : cmd_timing_list) {
                bank_states_[addr.rank][j][k].UpdateTiming(
                    cmd_timing.first, clk + cmd_timing.second);
            }
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateTimingAndStates(const Command& cmd, uint64_t clk) {
    UpdateState(cmd, clk);
    UpdateTiming(cmd, clk);

    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

bool ChannelState::ActivationWindowOk(int rank, uint64_t curr_time) const {
    bool tfaw_ok = IsFAWReady(rank, curr_time);
    if (config_.IsGDDR()) {
        if (!tfaw_ok)
            return false;
        else
            return Is32AWReady(rank, curr_time);
    }
    return tfaw_ok;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

void ChannelState::UpdateActivationTimes(int rank, uint64_t curr_time) {
    if (!four_aw_[rank].empty() && curr_time >= four_aw_[rank][0]) {
        four_aw_[rank].erase(four_aw_[rank].begin());
    }
    four_aw_[rank].push_back(curr_time + config_.tFAW);
    if (config_.IsGDDR()) {
        if (!thirty_two_aw_[rank].empty() &&
            curr_time >= thirty_two_aw_[rank][0]) {
            thirty_two_aw_[rank].erase(thirty_two_aw_[rank].begin());
        }
        thirty_two_aw_[rank].push_back(curr_time + config_.t32AW);
    }
    return;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

bool ChannelState::IsFAWReady(int rank, uint64_t curr_time) const {
    if (!four_aw_[rank].empty()) {
        if (curr_time < four_aw_[rank][0] && four_aw_[rank].size() >= 4) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

bool ChannelState::Is32AWReady(int rank, uint64_t curr_time) const {
    if (!thirty_two_aw_[rank].empty()) {
        if (curr_time < thirty_two_aw_[rank][0] &&
            thirty_two_aw_[rank].size() >= 32) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

}  // namespace dramsim3
