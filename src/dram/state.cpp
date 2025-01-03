/* author: Suhas Vittal
 *  date:   24 December 2024
 * */

#include "dram/address.h"
#include "dram/command.h"
#include "dram/state.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr static size_t BL = DRAM_BURST_LENGTH;

inline void 
update(uint64_t& t, uint64_t by)
{
    t = std::max(t, GL_DRAM_CYCLE+by);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
cmd_is_issuable(const DRAMChannelState& ch, const DRAMCommand& cmd)
{
    DRAMCommandType c = cmd.type;
    if (cmd_is_act(c) && ch.faw.size() == 4)
        return false;

    const auto& ra = ch.at(dram_rank(cmd.address));
    if (GL_DRAM_CYCLE >= ra.next_ref_cycle || GL_DRAM_CYCLE < ra.next_cmd_post_ref_cycle)
        return false;
    if (cmd_is_read(c) && GL_DRAM_CYCLE < ra.read_ok)
        return false;
    if (cmd_is_write(c) && GL_DRAM_CYCLE < ra.write_ok)
        return false;

    const auto& bg = ra.at(dram_bankgroup(cmd.address));
    if (cmd_is_read(c) && GL_DRAM_CYCLE < bg.read_ok)
        return false;
    if (cmd_is_write(c) && GL_DRAM_CYCLE < bg.write_ok)
        return false;
    if (c == DRAMCommandType::ACTIVATE && GL_DRAM_CYCLE < bg.act_ok)
        return false;
    
    const auto& ba = bg.at(dram_bank(cmd.address));
    if (cmd_is_act(c) && GL_DRAM_CYCLE < ba.act_ok)
        return false;
    if (cmd_is_cas(c) && GL_DRAM_CYCLE < ba.cas_ok)
        return false;
    if (cmd_is_pre_only(c) && GL_DRAM_CYCLE < ba.pre_ok)
        return false;

    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
update_dram_state(DRAMChannelState& ch, const DRAMCommand& cmd)
{
    DRAMCommandType c = cmd.type;

    uint64_t addr = cmd.address;

    auto& ra = ch.at(dram_rank(addr));
    auto& bg = ra.at(dram_bankgroup(addr));
    auto& ba = bg.at(dram_bank(addr));

    if (cmd_is_act(c))
        ch.faw.push_back(GL_DRAM_CYCLE);
    // Both rank and bankgroup states need to be updated for "other" ranks/bankgroups.
    update_dram_rank_states(ch, cmd);
    update_dram_bankgroup_states(ra, cmd);
    update_dram_bank_state(ba, cmd);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class PRED> inline bool
pred_all_banks(DRAMRankState& ra, const PRED& pred)
{
    return std::all_of(ra.begin(), ra.end(),
            [pred] (const auto& bg)
            {
                return std::all_of(bg.begin(), bg.end(), pred);
            });
}

template <class PRED> inline bool
pred_any_bank(DRAMRankState& ra, const PRED& pred)
{
    return std::any_of(ra.begin(), ra.end(),
            [pred] (const auto& bg)
            {
                return std::any_of(bg.begin(), bg.end(), pred);
            });
}

void
try_and_issue_ref(DRAMRankState& ra, uint64_t& s_ref, uint64_t& s_pre)
{
    bool preab_needed = pred_any_bank(ra, 
                            [] (const auto& ba) 
                            {
                                return ba.open_row.has_value();
                            });
    if (preab_needed)
    {
        bool all_ready = pred_all_banks(ra,
                            [] (const auto& ba)
                            {
                                return GL_DRAM_CYCLE >= ba.pre_ok;
                            });
        if (all_ready)
        {
            for (auto& bg : ra)
            {
                for (auto& ba : bg)
                {
                    if (ba.open_row.has_value())
                    {
                        update(ba.act_ok, tRP);
                        ba.open_row.reset();
                        ++s_pre;
                    }
                }
            }
        }
    }
    else  // Do refresh:
    {
        bool all_ready = pred_all_banks(ra,
                            [] (const auto& ba)
                            {
                                return GL_DRAM_CYCLE >= ba.act_ok;
                            });
        if (all_ready) 
        {
            update(ra.next_ref_cycle, tREFI);
            update(ra.next_cmd_post_ref_cycle, tRFC);
            ++s_ref;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr uint64_t OTHER_RANK_RTR = BL/2 + tRTRS;
constexpr uint64_t OTHER_RANK_RTW = CL + BL/2 + tRTRS - CWL;
constexpr uint64_t OTHER_RANK_WTR = CWL + BL/2 + tRTRS - CL;
constexpr uint64_t OTHER_RANK_WTW = BL/2;

void
update_dram_rank_states(DRAMChannelState& ch, const DRAMCommand& cmd)
{
    DRAMCommandType c = cmd.type;
    if (!cmd_is_cas(c))
        return;
    size_t raidx = dram_rank(cmd.address);
    
    for (size_t i = 0; i < ch.size(); i++)
    {
        if (i == raidx)
            continue;
        ch[i].read_ok = cmd_is_read(c) ? OTHER_RANK_RTR : OTHER_RANK_WTR;
        ch[i].write_ok = cmd_is_read(c) ? OTHER_RANK_RTW : OTHER_RANK_WTW;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define UPDATE_SL(t, s, l)  update(t, i == bgidx ? (s) : (l))

void
update_dram_bankgroup_states(DRAMRankState& ra, const DRAMCommand& cmd)
{
    DRAMCommandType c = cmd.type;

    size_t bgidx = dram_bankgroup(cmd.address);
    for (size_t i = 0; i < ra.size(); i++)
    {
        auto& bg = ra[i];

        if (cmd_is_act(c))
        {
            UPDATE_SL(bg.act_ok, tRRD_S, tRRD_L);
        }
        else if (cmd_is_read(c))
        {
            UPDATE_SL(bg.read_ok, tCCD_S, tCCD_L);
            UPDATE_SL(bg.write_ok, tCCD_S_RTW, tCCD_L_RTW);
        }
        else if (cmd_is_write(c))
        {
            UPDATE_SL(bg.read_ok, tCCD_S_WTR, tCCD_L_WTR);
            UPDATE_SL(bg.write_ok, tCCD_S_WR, tCCD_L_WR);
        }
    }
}

#undef UPDATE_SL

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
update_dram_bank_state(DRAMBankState& ba, const DRAMCommand& cmd)
{
    DRAMCommandType c = cmd.type;
    if (cmd_is_cas(c))
    {
        uint64_t cas_to_pre = cmd_is_read(c) ? tRTP : (CWL + BL/2 + tWR);
        if (cmd_is_autopre(c)) 
        {
            ba.open_row.reset();
            ba.num_cas_to_open_row = 0;
            
            ba.act_ok = std::max(ba.act_ok, std::max(GL_DRAM_CYCLE+cas_to_pre, ba.pre_ok)+tRP);
        } 
        else
        {
            ++ba.num_cas_to_open_row;
            update(ba.pre_ok, cas_to_pre);
            ba.next_cas_is_row_buffer_hit = true;
        }
    } 
    else if (cmd_is_act(c))
    {
        update(ba.cas_ok, tRCD);
        update(ba.pre_ok, tRAS);
        ba.open_row = dram_row(cmd.address);
        ba.next_cas_is_row_buffer_hit = false;
    }
    else // Precharge
    {
        ba.open_row.reset();
        ba.num_cas_to_open_row = 0;
        update(ba.act_ok, tRP);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
