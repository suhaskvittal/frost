/*
 *  author: Suhas Vittal
 *  date:   10 December 2024
 * */

#include "transaction.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
trans_is_read(TransactionType t)
{
    return t != TransactionType::WRITE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

Transaction::Transaction(uint8_t cid, const inst_ptr& inst, TransactionType t, uint64_t addr, bool addr_is_ip)
    :Transaction(cid, inst.get(), t, addr, addr_is_ip)
{}

Transaction::Transaction(uint8_t cid, raw_inst_ptr inst, TransactionType t, uint64_t addr, bool addr_is_ip)
    :coreid(cid),
    inst_list({inst}),
    type(t),
    address(addr),
    address_is_ip(addr_is_ip)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
Transaction::contains_inst(const inst_ptr& inst) const
{
    return contains_inst(inst.get());
}

bool
Transaction::contains_inst(raw_inst_ptr inst) const
{
    return std::find(inst_list.begin(), inst_list.end(), inst) != inst_list.end();
}

void
Transaction::merge(Transaction& y)
{
    std::move(y.inst_list.begin(), y.inst_list.end(), std::back_inserter(inst_list));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
