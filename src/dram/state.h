/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#ifndef DRAM_STATE_h
#define DRAM_STATE_h

#include "globals.h"
#include "dram_timing.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <optional>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct DRAMBankState
{
    using row_t = std::optional<uint64_t>;

    row_t open_row;

    uint64_t act_ok =0;
    uint64_t pre_ok =0;
    uint64_t cas_ok =0;

    size_t num_cas_to_open_row =0;
};

struct DRAMBankgroupState : public std::array<DRAMBankState, DRAM_BANKS>
{
    uint64_t read_ok =0;
    uint64_t write_ok =0;
    uint64_t act_ok =0;
};

struct DRAMRankState : public std::array<DRAMBankgroupState, DRAM_BANKGROUPS>
{
    uint64_t read_ok =0;
    uint64_t write_ok =0;

    uint64_t next_ref_cycle =tREFI;
    uint64_t next_cmd_post_ref_cycle =0;
};

struct DRAMChannelState : public std::array<DRAMRankState, DRAM_RANKS>
{
    using faw_t = std::deque<uint64_t>;

    faw_t faw;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct DRAMCommand;
/*
 * `cmd_is_issuable` and `update_dram_state` should be used to interact
 * with `DRAMChannelState`.
 *
 * `try_and_issue_ref` is a special function for handling all-bank refresh.
 * If any bank has an open row, PREab is first issued. Once all banks have
 * closed rows, then REFab is issued.
 * */
bool cmd_is_issuable(const DRAMChannelState&, const DRAMCommand&);
void update_dram_state(DRAMChannelState&, const DRAMCommand&);
void try_and_issue_ref(DRAMRankState&, uint64_t& s_ref, uint64_t& s_pre);
/*
 * These are just helper functions.
 * */
void update_dram_rank_states(DRAMChannelState&, const DRAMCommand&);
void update_dram_bankgroup_states(DRAMRankState&, const DRAMCommand&);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_STATE_h
