/*
 *  author: Suhas Vittal
 *  date:   15 January 2025
 * */

#ifndef DRAMSIM3_h
#define DRAMSIM3_h

#include "transaction.h"
#include "dramsim3.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

extern std::string OPT_DRAMSIM3_CONFIG_FILE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct DRAM
{
    using memsys_ptr = std::unique_ptr<dramsim3::MemorySystem>;

    memsys_ptr mem_;

    const double freq_ghz_;
private:
    using trans_map_type = std::unordered_multimap<uint64_t, Transaction>;

    trans_map_type pending_reads_;

    double leap_ =0.0;
    const double clock_scale_;
public:
    DRAM(double cpu_freq_ghz, double freq_ghz);

    void warmup_access(const Transaction&) {}

    bool can_accept(const Transaction&);
    bool add_incoming(Transaction);

    void tick(void);
    void print_stats(std::ostream&) { mem_->PrintStats(true); }

    inline bool deadlock_find_inst(const inst_ptr) const {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAMSIM3_h
