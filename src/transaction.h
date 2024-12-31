/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#ifndef TRANSACTION_h
#define TRANSACTION_h

#include <memory>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class TransactionType { READ, WRITE, PREFETCH, TRANSLATION };

bool trans_is_read(TransactionType);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Instruction;
using inst_ptr = Instruction*;
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
    using inst_list_t = std::vector<inst_ptr>;

    uint8_t         coreid;
    inst_list_t     inst_list;
    TransactionType type;

    uint64_t address;
    bool     address_is_ip;

    Transaction(uint8_t cid, inst_ptr, TransactionType, uint64_t addr, bool addr_is_ip=false);
    Transaction(const Transaction&) =default;

    bool contains_inst(inst_ptr) const;
    void merge(Transaction&);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // TRANSACTION_h
