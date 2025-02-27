/*
 *  author: Suhas Vittal
 *  date:   15 January 2025
 * */

#include "constants.h"
#include "globals.h"
#include "memsys.h"

#include "dramsim3_wrapper.h"
#include "util/numerics.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAM::DRAM(double cpu_freq_ghz, double freq_ghz)
    :freq_ghz_(freq_ghz),
    clock_scale_(cpu_freq_ghz/freq_ghz - 1.0)
{
    mem_ = memsys_ptr(new dramsim3::MemorySystem(OPT_DRAMSIM3_CONFIG_FILE, "",
                [this] (uint64_t byteaddress)
                {
                    uint64_t lineaddress = byteaddress >> ilog2(LINESIZE);
                    Transaction trans{NUM_THREADS, 0, lineaddress, nullptr, Transaction::Type::READ};
                    GL_LLC->add_incoming_fill(trans);
                },
                [] (uint64_t) {}));
    std::cout << "Initializing dramsim3 with config file: " << OPT_DRAMSIM3_CONFIG_FILE << "\n";                        
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAM::can_accept(const Transaction& trans)
{
    return mem_->WillAcceptTransaction(trans.address << ilog2(LINESIZE), trans.is_write());
}

bool
DRAM::add_incoming(Transaction trans)
{
    mem_->AddTransaction(trans.address << ilog2(LINESIZE), trans.is_write());
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAM::tick()
{
    if (leap_ >= 1.0)
        leap_ -= 1.0;
    else
    {
        mem_->ClockTick();
        leap_ += clock_scale_;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
