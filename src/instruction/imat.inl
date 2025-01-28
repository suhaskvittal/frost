/*
 *  author: Suhas Vittal
 *  date:   28 January 2025
 * */

#include <cstring>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Instruction : public INST_BASE
{
    uint64_t ip =0;
    uint64_t v_lineaddr =0;
    bool     is_store =false;

    uint64_t p_lineaddr;

    AccessState state =AccessState::READY;

    inline Instruction(const IMAT& t)
    {
        memcpy(&inst_num, t.inst_num, sizeof(t.inst_num));
        memcpy(&ip, t.ip, sizeof(t.ip));
        memcpy(&v_lineaddr, &t.v_lineaddr, sizeof(t.v_lineaddr));
        is_store = t.is_write;
    }

    inline bool is_mem_inst(void) const
    {
        return true;
    }

    inline bool is_done(void) const
    {
        return is_store ? (state == AccessState::IN_CACHE) : (state == AccessState::DONE);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
