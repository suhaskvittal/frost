/*
 *  author: Suhas Vittal
 *  date:   15 December 2024
 * */

#ifndef DRAM_BANK_h
#define DRAM_BANK_h

#include "dram/command.h"

#include <cstdint>
#include <deque>
#include <optional>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct DRAMBank
{
    using row_t = std::optional<uint64_t>;
    using cmd_queue_t = std::deque<DRAMCommand>;

    row_t open_row_;
    size_t num_cas_to_open_row_ =0;

    cmd_queue_t cmd_queue_;
    /*
     * command queue metadata (not used by all policies)
     * */
    size_t num_writes_in_cmdq_ =0;
    size_t write_draining_ =0;

    uint64_t act_ok_cycle_ =0;
    uint64_t pre_ok_cycle_ =0;
    uint64_t cas_ok_cycle_ =0;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMCmdQueuePolicy { FCFS, FRFCFS, FRRFCFS, ARRFCFS };

using sel_cmd_t = std::optional<DRAMCommand>;
using cmdq_iterator = DRAMBank::cmd_queue_t::iterator;

sel_cmd_t FCFS(cmdq_iterator, DRAMBank&);
sel_cmd_t FRFCFS(cmdq_iterator, DRAMBank&);
sel_cmd_t FRRFCFS(cmdq_iterator, DRAMBank&);
sel_cmd_t ARRFCFS(cmdq_iterator, DRAMBank&);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_BANK_h
