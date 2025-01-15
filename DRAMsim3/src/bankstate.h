#ifndef __BANKSTATE_H
#define __BANKSTATE_H

#include <vector>
#include "common.h"
#include "configuration.h"
#include "simple_stats.h"

namespace dramsim3 {

// [MIRZA]
struct MIRZA_Q_Entry {
    uint32_t rowid;
    uint16_t groupid;
    uint16_t actctr;

    std::string to_string() const
    {
        return "RowID: " + std::to_string(rowid) + " GroupID: " + std::to_string(groupid) + " ActCtr: " + std::to_string(actctr);
    }
};

class BankState {
public:
    BankState(const Config& config, SimpleStats& simple_stats, int rank, int bank_group, int bank);

    enum class State { OPEN, CLOSED, SREF, PD, SIZE };
    Command GetReadyCommand(const Command& cmd, uint64_t clk) const;

    // Update the state of the bank resulting after the execution of the command
    void UpdateState(const Command& cmd, uint64_t clk);

    // Update the existing timing constraints for the command
    void UpdateTiming(const CommandType cmd_type, uint64_t time);

    bool IsRowOpen() const { return state_ == State::OPEN; }
    int OpenRow() const { return open_row_; }
    int RowHitCount() const { return row_hit_count_; }

    std::string StateToString(State state) const;
    void PrintState() const;

    bool CheckAlert();
private:
    void MoatCheckTrackedRowIsRefreshed(void);

    void MoatUpdate(void);
    void MoatMitigate(void);
    void MoatHandleRef(void);

    void MopacFlushNextCtr(void);
    void MopacFlushAllCtrs(void);
    void MopacHandleRef(void);
    void MopacMitigate(void);
    void MopacCtrUpdate(void);

    const Config& config_;
    SimpleStats& simple_stats_;

    // Current state of the Bank
    // Apriori or instantaneously transitions on a command.
    State state_;

    // Earliest time when the particular Command can be executed in this bank
    std::vector<uint64_t> cmd_timing_;

    // Currently open row
    int open_row_;

    // consecutive accesses to one row
    int row_hit_count_;

    // rank, bank group, bank
    int rank_;
    int bank_group_;
    int bank_;

    // [RFM] RAA counter (Rolling Accumulated ACT)
    int raa_ctr_;

    // Activations
    std::string acts_stat_name_;
    int acts_counter_;
    /*
     * Per-row activation counters (PRAC)
     * */
    std::vector<uint16_t> prac_;
    /*
     * Data structures for MOAT implementation of PRAC.
     * */
    bool moat_row_valid_ =false;
    size_t moat_row_;

    size_t moat_eth_;
    size_t moat_ath_;
    size_t moat_trefi_cnt_ =0;

    size_t mopac_act_ctr_ =0;
    size_t mopac_mint_sel_;
    /*
     * `mopac_buf_` stores up-to 5 rows that will receive counter updates.
     * */
    bool alert_sent_due_to_full_mopac_buf_ =false;
    std::vector<size_t> mopac_buf_;

    // [REF]
    uint32_t ref_idx_;
};

}  // namespace dramsim3
#endif
