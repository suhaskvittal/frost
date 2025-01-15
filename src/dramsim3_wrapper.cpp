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

DRAM::IO::IO(DRAM* d)
    :dram(d)
{}

bool
DRAM::IO::can_accept(uint64_t address, TransactionType t)
{
    return dram->mem_->WillAcceptTransaction(address << numeric_traits<LINESIZE>::log2, trans_is_write(t));
}

bool
DRAM::IO::add_incoming(Transaction t)
{
    dram->mem_->AddTransaction((t.address << numeric_traits<LINESIZE>::log2), trans_is_write(t.type));
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAM::DRAM(double cpu_freq_ghz, double freq_ghz)
    :io_(new DRAM::IO(this)),
    freq_ghz_(freq_ghz),
    clock_scale_(cpu_freq_ghz/freq_ghz - 1.0)
{
    mem_ = memsys_ptr(new dramsim3::MemorySystem(OPT_DRAMSIM3_CONFIG_FILE, "",
                [this] (uint64_t byteaddress)
                {
                    uint64_t lineaddress = byteaddress >> numeric_traits<LINESIZE>::log2;
                    GL_LLC->mark_load_as_done(lineaddress);
                },
                [] (uint64_t) {}));
    std::cout << "Initializing dramsim3 with config file: " << OPT_DRAMSIM3_CONFIG_FILE << "\n";                        
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
