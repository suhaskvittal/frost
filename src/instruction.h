/*
 *  author: Suhas Vittal
 *  date:   11 January 2025
 * */

#ifndef INSTRUCTION_h
#define INSTRUCTION_h

#include "trace/fmt.h"

#include <cstdint>
#include <cstddef>
#include <limits>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class AccessState
{
    NOT_READY =0,
    IN_TLB    =1,
    READY     =2,
    IN_CACHE  =3,
    DONE      =4,
    SIZE      =5
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * This contains the basic information required for an instruction:
 * */
struct INST_BASE
{
    uint64_t inst_num =0;

    size_t   rob_refs =1;
    uint64_t cycle_done =std::numeric_limits<uint64_t>::max();

    virtual bool is_mem_inst(void) =0 const;
    virtual bool is_done(void) =0 const;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#if   TRACE_FORMAT == CTF

#include "instruction/ctf.inl"

#elif TRACE_FORMAT == MTF

#include "instruction/mtf.inl"

#elif TRACE_FORMAT == IMAT

#include "instruction/imat.inl"

#endif

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Instruction
{
    uint64_t inst_num =0;
    uint64_t v_lineaddr =0;
    bool     is_store;

    uint64_t p_lineaddr;

    size_t rob_refs =1;
    AccessState state =AccessState::READY;
    uint64_t cycle_done =std::numeric_limits<uint64_t>::max();

    Instruction(const MemsimTraceFormat&);

    bool is_mem_inst(void) const;
    bool is_done(void) const;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * While using a raw pointer is not ideal, we find that it yields the best performance. We
 * always know where to delete `inst_ptr` (after it is retired from the ROB).
 *
 * Using a unique_ptr causes overheads in lambdas.
 * Using a shared_ptr has too many overheads due to ownership tracking.
 * */
using inst_ptr = Instruction*;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // INSTRUCTION_h
