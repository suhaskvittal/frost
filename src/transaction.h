/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#ifndef TRANSACTION_h
#define TRANSACTION_h

#include "instruction.h"
#include "dram/enums.h"

#include <cstdint>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * This struct should have all information for routing cache/memory requests
 * through the memory hierarchy.
 *
 * Note we store `Instruction` as a raw pointer even though we store `Instruction`
 * with a `unique_ptr` in `Core`. This is because we know that the ptr in `Core`
 * will go out of scope once it is retired.
 * */
struct Transaction
{
    enum class Type { READ, WRITE, PREFETCH, INSTRUCTION, TRANSLATION };
    /*
     * Explanation of unclear data:
     *  `ip`: if `inst != nullptr`, then this is `inst->ip`. Otherwise, this is a writeback,
     *          and is the ip of the evicting instruction.
     *  `type`: what type of instruction. Used to determine priority and where to send the data.
     * */
    uint8_t  coreid;
    uint64_t ip;
    uint64_t address;
    inst_ptr inst;
    Type     type;
    /*
     * Other optional variables:
     * */
    int8_t dram_issue_prio =0;

    inline bool is_read(void) const
    {
        return type != Type::WRITE;
    }
    
    inline bool is_write(void) const
    {
        return type == Type::WRITE;
    }

    inline bool is_prefetch(void) const
    {
        return type == Type::PREFETCH;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // TRANSACTION_h
