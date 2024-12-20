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
    using queue_t = std::deque<DRAMCommand>;
    /*
     * Different methods of implementing a command queue:
     *  `unified` places reads and writes into the same queue
     *  `split` separates reads and writes into two different queues.
     * */
    union cmd_queue_impl
    {
        queue_t unified;

        struct
        {
            queue_t reads;
            queue_t writes;
            size_t writes_to_drain =0;
        } split;
    };

    row_t open_row_;
    size_t num_cas_to_open_row_ =0;

    cmd_queue_impl cmd_queue_;

    uint64_t act_ok_cycle_ =0;
    uint64_t pre_ok_cycle_ =0;
    uint64_t cas_ok_cycle_ =0;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMCmdQueuePolicy { FCFS, FRFCFS, FRRFCFS, ARRFCFS };

using sel_cmd_t = std::optional<DRAMCommand>;
using cmdq_iterator = DRAMBank::cmd_queue_t::const_iterator;

sel_cmd_t FCFS(cmdq_iterator, DRAMBank&);
sel_cmd_t FRFCFS(const DRAMBank::queue_t&, cmdq_iterator, DRAMBank&);
sel_cmd_t FRRFCFS(const DRAMBank::queue_t&, cmdq_iterator, DRAMBank&, bool any_read_hits_in_queue);
sel_cmd_t ARRFCFS(const DRAMBank::queue_t&, cmdq_iterator, DRAMBank&, bool any_reads_in_queue, bool is_first_read);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_BANK_h
