/*
 *  author: Suhas Vittal
 *  date:   3 December 2024
 * */

#ifndef TRANSACTION_h
#define TRANSACTION_h

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class TransactionType { READ, WRITE, PREFETCH, TRANSLATION };

inline bool trans_is_read(TransactionType& t)
{
    return t != TransactionType::WRITE;
}

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
    uint8_t         coreid;
    Instruction*    inst;
    TransactionType type;

    uint64_t address;
    bool     address_is_ip;

    Transaction(uint8_t cid, Instruction* inst, TransactionType t, uint64_t addr, bool addr_is_ip=false)
        :coreid(cid),
        inst(inst),
        type(t),
        address(addr),
        address_is_ip(addr_is_ip)
    {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // TRANSACTION_h
