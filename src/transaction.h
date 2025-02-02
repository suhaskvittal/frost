/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#ifndef TRANSACTION_h
#define TRANSACTION_h

#include "instruction.h"
#include "dram/enums.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class TransactionType { READ, WRITE, PREFETCH, TRANSLATION };

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
    using inst_list_type = std::vector<inst_ptr>;

    uint8_t         coreid;
    inst_list_type  inst_list;
    TransactionType type;

    uint64_t address;
    bool     address_is_ip;

    DRAMClosureHint dram_closure_hint =DRAMClosureHint::NONE;

    Transaction(uint8_t cid, inst_ptr inst, TransactionType t, uint64_t addr, bool addr_is_ip=false)
        :coreid(cid),
        inst_list({inst}),
        type(t),
        address(addr),
        address_is_ip(addr_is_ip)
    {}

    Transaction(const Transaction&) =default;

    inline bool contains_inst(inst_ptr inst) const
    {
        return std::find(inst_list.begin(), inst_list.end(), inst) != inst_list.end();
    }

    inline void merge(Transaction& t)
    {
        std::move(t.inst_list.begin(), t.inst_list.end(), std::back_inserter(inst_list));
    }

    inline uint64_t get_front_ip(void) const
    {
#if defined(TRACE_FORMAT_MTF)
        return 0;
#else
        return inst_list.at(0)->ip;
#endif
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool trans_is_read(TransactionType t)
{
    return t != TransactionType::WRITE;
}

inline bool trans_is_write(TransactionType t)
{
    return t == TransactionType::WRITE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // TRANSACTION_h
