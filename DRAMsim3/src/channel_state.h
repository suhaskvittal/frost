#ifndef __CHANNEL_STATE_H
#define __CHANNEL_STATE_H

#include <vector>
#include "bankstate.h"
#include "common.h"
#include "configuration.h"
#include "timing.h"
#include "simple_stats.h"


namespace dramsim3 {

class ChannelState {
   public:
    ChannelState(const Config& config, const Timing& timing, SimpleStats& simple_stats);
    Command GetReadyCommand(const Command& cmd, uint64_t clk) const;

    void UpdateState(const Command& cmd, uint64_t clk);
    void UpdateTiming(const Command& cmd, uint64_t clk);
    void UpdateTimingAndStates(const Command& cmd, uint64_t clk);

    bool ActivationWindowOk(int rank, uint64_t curr_time) const;
    void UpdateActivationTimes(int rank, uint64_t curr_time);

    bool IsAllBankIdleInRank(int rank) const;

    bool IsRWPendingOnRef(const Command& cmd) const;
    void BankNeedRefresh(int rank, int bankgroup, int bank, bool need);
    void BanksetNeedRefresh(int rank, int bank, bool need);
    void RankNeedRefresh(int rank, bool need);

    bool IsRowOpen(int rank, int bankgroup, int bank) const { return bank_states_[rank][bankgroup][bank].IsRowOpen(); }
    bool IsRankSelfRefreshing(int rank) const { return rank_is_sref_[rank]; }
    bool IsRefreshWaiting() const { return !refresh_q_.empty(); }

    const Command& PendingRefCommand() const {return refresh_q_.front(); }
    
    // [RFM]
    void RankNeedRFM(int rank, bool need);
    void BanksetNeedRFM(int rank, int bank, bool need); 
    bool IsRFMWaiting() const { return !rfm_q_.empty(); }
    const Command& PendingRFMCommand() const {return rfm_q_.front(); }
    
    int OpenRow(int rank, int bankgroup, int bank) const {
        return bank_states_[rank][bankgroup][bank].OpenRow();
    }
    int RowHitCount(int rank, int bankgroup, int bank) const {
        return bank_states_[rank][bankgroup][bank].RowHitCount();
    };

    void PrintDeadlock() const;

    std::vector<int> rank_idle_cycles;

   private:
    const Config& config_;
    const Timing& timing_;
    SimpleStats& simple_stats_;

    std::vector<bool> rank_is_sref_;
    std::vector<std::vector<std::vector<BankState> > > bank_states_;
    std::vector<Command> refresh_q_;
    std::vector<Command> rfm_q_; // [RFM]

    std::vector<std::vector<uint64_t> > four_aw_;
    std::vector<std::vector<uint64_t> > thirty_two_aw_;
    /*
     * Alert-Backoff (ABO) implementation
     * */
    bool alert_n =false;
    uint64_t last_alert_clk_ =0;
    int num_acts_abo_ =0;

    inline void ResetAlert() { alert_n = false; }

    bool IsFAWReady(int rank, uint64_t curr_time) const;
    bool Is32AWReady(int rank, uint64_t curr_time) const;
    
    void TriggerSameBankAlert(const Command& cmd, uint64_t clk);
    void TriggerSameRankAlert(const Command& cmd, uint64_t clk);

    void UpdateSameBankset(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);
    
    void UpdateOtherBanksets(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of the bank the command corresponds to    
    void UpdateSameBankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of the other banks in the same bankgroup as the command
    void UpdateOtherBanksSameBankgroupTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of banks in the same rank but different bankgroup as the
    // command
    void UpdateOtherBankgroupsSameRankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of banks in a different rank as the command
    void UpdateOtherRanksTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);

    // Update timing of the entire rank (for rank level commands)
    void UpdateSameRankTiming(
        const Address& addr,
        const std::vector<std::pair<CommandType, int> >& cmd_timing_list,
        uint64_t clk);
};

}  // namespace dramsim3
#endif
