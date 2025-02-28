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
    const auto& ra = ch.at(dram_rank(cmd.address));
    const auto& bg = ra.at(dram_bankgroup(cmd.address));
    const auto& ba = bg.at(dram_bank(cmd.address));

    bool ok = true;

    // Check that the rank is not stalled by REF
    ok &= (GL_DRAM_CYCLE < ra.next_ref_cycle && GL_DRAM_CYCLE > ra.next_cmd_post_ref_cycle);
    
    // Check ACT conditions:
    ok &= !cmd.is_act() || (ra.faw.size() < 4 && GL_DRAM_CYCLE >= bg.act_ok && GL_DRAM_CYCLE >= ba.act_ok);
    
    // CAS conditions:
    ok &= !cmd.is_read() || (GL_DRAM_CYCLE >= ra.read_ok && GL_DRAM_CYCLE >= bg.read_ok);
    ok &= !cmd.is_write() || (GL_DRAM_CYCLE >= ra.write_ok && GL_DRAM_CYCLE >= bg.write_ok);
    ok &= !cmd.is_cas() || (GL_DRAM_CYCLE >= ba.cas_ok);

    // PRE conditions:
    ok &= !cmd.is_pre_only() || (GL_DRAM_CYCLE >= ba.pre_ok);

    return ok;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
update_dram_state(DRAMChannelState& ch, const DRAMCommand& cmd)
{
    uint64_t addr = cmd.address;

    auto& ra = ch.at(dram_rank(addr));
    auto& bg = ra.at(dram_bankgroup(addr));
    auto& ba = bg.at(dram_bank(addr));

    if (cmd.is_act())
        ra.faw.push_back(GL_DRAM_CYCLE);
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

bool
try_and_issue_ref(DRAMRankState& ra, uint32_t& s_ref, uint32_t& s_pre)
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
            return true;
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
    return false;
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
    if (!cmd.is_cas())
        return;
    size_t raidx = dram_rank(cmd.address);
    
    for (size_t i = 0; i < ch.size(); i++)
    {
        if (i == raidx)
            continue;
        ch[i].read_ok = cmd.is_read() ? OTHER_RANK_RTR : OTHER_RANK_WTR;
        ch[i].write_ok = cmd.is_read() ? OTHER_RANK_RTW : OTHER_RANK_WTW;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define UPDATE_SL(t, s, l)  update(t, i == bgidx ? (l) : (s))

void
update_dram_bankgroup_states(DRAMRankState& ra, const DRAMCommand& cmd)
{
    size_t bgidx = dram_bankgroup(cmd.address);
    for (size_t i = 0; i < ra.size(); i++)
    {
        auto& bg = ra[i];

        if (cmd.is_act())
        {
            UPDATE_SL(bg.act_ok, tRRD_S, tRRD_L);
        }
        else if (cmd.is_read())
        {
            UPDATE_SL(bg.read_ok, tCCD_S, tCCD_L);
            UPDATE_SL(bg.write_ok, tCCD_S_RTW, tCCD_L_RTW);
        }
        else if (cmd.is_write())
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
    if (cmd.is_cas())
    {
        if (!ba.open_row.has_value() || ba.open_row.value() != dram_row(cmd.address))
        {
            std::cerr << "[ update_dram_bank_state ] received CAS command but no row is open\n";
            exit(1);
        }
            
        uint64_t cas_to_pre = cmd.is_read() ? tRTP : (CWL + BL/2 + tWR);
        if (cmd.autopre) 
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
    else if (cmd.is_act())
    {
        if (ba.open_row.has_value())
        {
            std::cerr << "[ update_dram_bank_state ] received ACT but row is already open\n";
            exit(1);
        }

        update(ba.cas_ok, tRCD);
        update(ba.pre_ok, tRAS);
        ba.open_row = dram_row(cmd.address);
        ba.next_cas_is_row_buffer_hit = false;
    }
    else // Precharge
    {
        if (!ba.open_row.has_value())
        {
            std::cerr << "[ update_dram_bank_state ] received PRE but row is already closed\n";
            exit(1);
        }

        ba.open_row.reset();
        ba.num_cas_to_open_row = 0;
        update(ba.act_ok, tRP);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
