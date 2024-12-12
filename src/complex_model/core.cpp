/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "globals.h"
#include "memsys.h"

#include "complex_model/core.h"
#include "complex_model/os.h"
#include "trace/reader.h"
#include "os/address.h"
#include "util/stats.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t DEADLOCK_CYCLES = 500'000;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline uint64_t
LINEADDR(uint64_t byteaddr)
{
    return byteaddr >> numeric_traits<LINESIZE>::log2;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline std::string
cache_name(std::string base, uint8_t coreid)
{
    return base + "_C" + std::to_string(static_cast<int>(coreid));
}

Core::Core(uint8_t coreid, std::string trace_file)
    :coreid_(coreid),
    trace_file(trace_file),
    trace_reader_(new TraceReader(trace_file))
{
    // Initialize caches.
    L2_ = l2_ptr(new L2Cache(cache_name("L2", coreid), GL_LLC));
    L1I_ = l1i_ptr(new L1ICache(cache_name("L1i", coreid), L2_));
    L1D_ = l1d_ptr(new L1DCache(cache_name("L1d", coreid), L2_));
}

Core::~Core() {}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick_warmup()
{
    iptr_t inst = next_inst(true);
    // First do icache access
    inst->pip = GL_OS->warmup_translate_ip(coreid_, inst->ip);
    L1I_->warmup_access(LINEADDR(inst->pip), false);
    // Now do data access
    for (Memop& x : inst->loads) {
        x.p_lineaddr = GL_OS->warmup_translate_ldst(this->coreid_, x.v_lineaddr);
        L1D_->warmup_access(x.p_lineaddr, false);
    }
    for (Memop& x : inst->stores) {
        x.p_lineaddr = GL_OS->warmup_translate_ldst(this->coreid_, x.v_lineaddr);
        L1D_->warmup_access(x.p_lineaddr, true);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::tick()
{
    operate_caches();
    operate_rob();
    for (size_t i = 0; i < CORE_FETCH_WIDTH; i++) {
        disp(i);
        ifmem(i);
        iftr(i);
        ifbp(i);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
 
template <class CACHE> inline void
print_cache_stats_for_core(Core* c, const std::unique_ptr<CACHE>& cache, std::ostream& out, std::string_view header)
{
    uint8_t id = c->coreid_;
    uint64_t inst = c->finished_inst_num_;
    
    uint64_t accesses = cache->s_accesses_.at(id),
             misses = cache->s_misses_.at(id),
             invalidates = cache->s_invalidates_.at(id),
             write_alloc = cache->s_write_alloc_.at(id);

    double apki = mean(accesses, inst) * 1000.0,
           mpki = mean(misses, inst) * 1000.0;
    double miss_penalty = misses == 0 ? 0.0 : mean(cache->s_tot_penalty_.at(id), cache->s_num_penalty_.at(id));
    double miss_rate = mean(misses, accesses);
    double aat = CACHE::CACHE_LATENCY * (1-miss_rate) + miss_penalty*miss_rate;

    uint64_t write_blocked_cycles = cache->io_->s_blocking_writes_ / CACHE::NUM_RW_PORTS;

    out << std::setw(16) << std::left << header
        << std::setw(16) << std::left << accesses
        << std::setw(16) << std::left << misses
        << std::setw(16) << std::left << std::setprecision(3) << miss_rate
        << std::setw(16) << std::left << invalidates
        << std::setw(16) << std::left << write_alloc
        << std::setw(16) << std::left << std::setprecision(3) << apki
        << std::setw(16) << std::left << std::setprecision(3) << mpki
        << std::setw(16) << std::left << std::setprecision(3) << aat
        << std::setw(16) << std::left << std::setprecision(3) << miss_penalty
        << std::setw(16) << std::left << cache->s_writebacks_
        << std::setw(16) << std::left << write_blocked_cycles
        << "\n";
}

void
Core::checkpoint_stats()
{
    double ipc = mean(finished_inst_num_, GL_CYCLE);

    std::string header = "CORE_" + std::to_string(static_cast<int>(coreid_));

    stats_stream_ << BAR << "\n";

    print_stat(stats_stream_, header, "INST", finished_inst_num_);
    print_stat(stats_stream_, header, "CYCLES", GL_CYCLE);
    print_stat(stats_stream_, header, "IPC", ipc);

    print_stat(stats_stream_, header, "IFMEM_STALLS", s_ifmem_stalls_);
    print_stat(stats_stream_, header, "DISP_STALLS", s_disp_stalls_);

    stats_stream_ << BAR << "\n";

    stats_stream_ << std::setw(16) << std::left << "CACHE"
        << std::setw(16) << std::left << "ACCESSES"
        << std::setw(16) << std::left << "MISSES"
        << std::setw(16) << std::left << "MISS_RATE"
        << std::setw(16) << std::left << "INVALIDATES"
        << std::setw(16) << std::left << "WRITE_ALLOC"
        << std::setw(16) << std::left << "APKI"
        << std::setw(16) << std::left << "MPKI"
        << std::setw(16) << std::left << "AAT"
        << std::setw(16) << std::left << "MISS_PENALTY"
        << std::setw(16) << std::left << "WRITEBACKS"
        << std::setw(16) << std::left << "WRITE_BLOCKED"
        << "\n" << BAR << "\n";

    print_cache_stats_for_core(this, L1I_, stats_stream_, header + "_L1I$");
    print_cache_stats_for_core(this, L1D_, stats_stream_, header + "_L1D$");
    print_cache_stats_for_core(this, L2_, stats_stream_, header + "_L2$");
    print_cache_stats_for_core(this, GL_LLC, stats_stream_, header + "_LLC");
    print_cache_stats_for_core(this, GL_OS->ITLB_[coreid_], stats_stream_, header + "_iTLB");
    print_cache_stats_for_core(this, GL_OS->DTLB_[coreid_], stats_stream_, header + "_dTLB");
    print_cache_stats_for_core(this, GL_OS->L2TLB_[coreid_], stats_stream_, header + "_L2TLB");
}

void
Core::print_stats(std::ostream& out)
{
    out << stats_stream_.str();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::ifbp(size_t fwid)
{
    Latch& la = la_ifbp_iftr_[fwid];
    if (la.stalled)
        return;
    iptr_t inst = next_inst();
    inst->cycle_fetched = GL_CYCLE;

    la.inst = std::move(inst);
    la.valid = true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::iftr(size_t fwid)
{
    Latch& la = la_ifbp_iftr_[fwid],
         & la_next = la_iftr_ifmem_[fwid];
    la.stalled = false;
    if (!la.valid)
        return;

    iptr_t& inst = la.inst;
    if (la_next.stalled || !GL_OS->translate_ip(coreid_, inst)) {
        ++s_iftr_stalls_;
        la.stalled = true;
        return;
    }
    inst->ip_state = AccessState::IN_TLB;

    la.valid = false;
    la_next.inst = std::move(la.inst);
    la_next.valid = true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::ifmem(size_t fwid)
{
    Latch& la = la_iftr_ifmem_[fwid];
    la.stalled = false;
    if (!la.valid)
        return;
    Transaction t(coreid_, la.inst, TransactionType::READ, LINEADDR(la.inst->pip), true);
    // Cannot proceed if the ip has not been translated or if
    // we cannot submit `t`.
    iptr_t& inst = la.inst;
    if (inst->ip_state != AccessState::READY 
            || ftb_.size() == CORE_FTB_SIZE
            || !L1I_->io_->add_incoming(t)) 
    {
        if (GL_CYCLE - inst->cycle_fetched >= DEADLOCK_CYCLES) {
            std::cerr << "\ncore: ifmem deadlock @ cycle = " << GL_CYCLE
                    << " for instruction #" << inst->inst_num
                    << ", ip = " << inst->ip
                    << " in core " << coreid_+0
                    << "\n\tftb size = " << ftb_.size()
                    << "\n";
            GL_OS->ITLB_[coreid_]->deadlock_find_inst(inst);
            GL_OS->L2TLB_[coreid_]->deadlock_find_inst(inst);
            exit(1);
        }
        la.stalled = true;
        ++s_ifmem_stalls_;
        return;
    }
    la.valid = false;
    // Place into the fetch target buffer.
    ftb_.push_back(std::move(inst));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::disp(size_t fwid)
{
    if (ftb_.empty())
        return;

    auto& inst = ftb_.front();
    if (inst->ip_state != AccessState::DONE || rob_.size() == CORE_ROB_SIZE) {
        if (GL_CYCLE - inst->cycle_fetched >= DEADLOCK_CYCLES) {
            std::cerr << "\ncore: disp deadlock @ cycle = " << GL_CYCLE
                    << " for instruction #" << inst->inst_num
                    << ", ip = " << inst->ip
                    << " in core " << coreid_+0
                    << "\n\trob size = " << rob_.size()
                    << "\n";
            L1I_->deadlock_find_inst(inst);
            L2_->deadlock_find_inst(inst);
            GL_LLC->deadlock_find_inst(inst);
            exit(1);
        }
        ++s_disp_stalls_;
        return;
    }
    // Otherwise, install into the ROB.
    inst->cycle_issued = GL_CYCLE;
    // Check if `la.inst` is a memory instruction. 
    // If not, then set its completion cycle to next cycle.
    if (!inst->is_mem_inst())
        inst->cycle_done = GL_CYCLE+1;
    rob_.push_back(std::move(inst));
    ftb_.pop_front();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::operate_rob()
{
    // First, check if we can complete any ROB entries.
    for (size_t i = 0; i < CORE_FETCH_WIDTH && !rob_.empty(); i++) {
        iptr_t& inst = rob_.front();

        if (inst->cycle_rob_head == std::numeric_limits<uint64_t>::max())
            inst->cycle_rob_head = GL_CYCLE;

        if (GL_CYCLE < inst->cycle_done) {
            if (GL_CYCLE - inst->cycle_rob_head > DEADLOCK_CYCLES) {
                uint64_t loads_remaining = inst->loads.size() 
                                            - inst->num_loads_in_state[static_cast<int>(AccessState::DONE)];
                uint64_t stores_remaining = inst->stores.size()
                                            - inst->num_stores_in_state[static_cast<int>(AccessState::IN_CACHE)];

                std::cerr << "\ncore: rob deadlock @ cycle = " << GL_CYCLE
                        << " for instruction #" << inst->inst_num
                        << ", ip = " << inst->ip
                        << " in core " << coreid_+0 << "\n"
                        << "\tcycle done = " << inst->cycle_done << "\n"
                        << "\tloads = " << inst->loads.size() << "\n"
                        << "\tstores = " << inst->stores.size() << "\n"
                        << "\tloads remaining = " << loads_remaining << "\n"
                        << "\tstores remaining = " << stores_remaining << "\n";
                L1D_->deadlock_find_inst(inst);
                L2_->deadlock_find_inst(inst);
                GL_LLC->deadlock_find_inst(inst);
                exit(1);
            }
            break;
        }
        inst->retired = true;
        rob_.pop_front();
        ++finished_inst_num_;
    }
    // Nothing to be done if the rob is empty
    if (rob_.empty())
        return;
    // Check if any ROB entries can access the L1D$ or
    // need address translation.
    for (iptr_t& inst : rob_) {
        if (GL_CYCLE >= inst->cycle_done)
            continue;

        inst_dcache_access(inst, coreid_, L1D_);
        inst_dtlb_access(inst, coreid_);

        if (inst->loads.empty() && inst->is_done())
            inst->cycle_done = GL_CYCLE+1;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
Core::operate_caches()
{
    // First, drain L2 outgoing queue.
    drain_cache_outgoing_queue(L2_, 
            [this] (const Transaction& t)
            {
                if (t.address_is_ip)
                    this->L1I_->mark_load_as_done(t.address);
                else
                    this->L1D_->mark_load_as_done(t.address);
            });
    // Handle L1I outgoing queue
    drain_cache_outgoing_queue(L1I_,
            [] (const Transaction& t)
            {
                for (auto inst : t.inst_list) {
                    if (inst->retired) {
                        std::cerr << "\ncore: L1i$ zombie wakeup @ cycle = " << GL_CYCLE
                                << " to instruction #" << inst->inst_num
                                << ", ip = " << inst->ip 
                                << " in core " << t.coreid+0 << "\n";
                        exit(1);
                    }
                    inst->ip_state = AccessState::DONE;
                }
            });
    // Handle L1D outgoing queue
    drain_cache_outgoing_queue(L1D_,
            [] (const Transaction& t)
            {
                // Intercept any accesses that should go the PTW.
                if (t.type == TransactionType::TRANSLATION) {
                    GL_OS->handle_l1d_outgoing(t.coreid, t);
                    return;
                }

                for (auto inst : t.inst_list) {
                    if (inst->retired) {
                        std::cerr << "\ncore: L1d$ zombie wakeup @ cycle = " << GL_CYCLE
                                << " to instruction #" << inst->inst_num
                                << ", ip = " << inst->ip 
                                << ", in core " << t.coreid+0 << "\n"
                                << "\tpip = " << inst->pip << "\n"
                                << "\tload to address: " << t.address << "\n"
                                << "\tnum loads = " << inst->loads.size() << "\n"
                                << "\tnum stores = " << inst->stores.size() << "\n"
                                << "\tloads:";
                        for (const auto& x : inst->loads)
                            std::cerr << " " << x.p_lineaddr;
                        std::cerr << "\n";
                        exit(1);
                    }
                    // Update `loads`.
                    ++inst->num_loads_in_state[static_cast<int>(AccessState::DONE)];
                    if (inst->is_done())
                        inst->cycle_done = GL_CYCLE;
                }
            });
    // Tick each of the caches.
    L2_->tick();
    L1I_->tick();
    L1D_->tick();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

iptr_t
Core::next_inst(bool warmup)
{
    iptr_t inst = iptr_t(new Instruction(inst_num_, trace_reader_->read())); 
    if (!warmup)
        ++inst_num_;
    // Tag the ip, load, and store addresses with the coreid.
    return inst;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
inst_dtlb_access(iptr_t& inst, uint8_t coreid)
{
    auto func = [inst, coreid] (Instruction::memop_list_t& v)
    {
        for (Memop& x : v) {
            if (x.state == AccessState::NOT_READY && GL_OS->translate_ldst(coreid, inst, x.v_lineaddr))
                x.state = AccessState::IN_TLB;
        }
    };
    
    inst_do_func_dependent_on_state<AccessState::IN_TLB>(inst, func);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
inst_dtlb_done(iptr_t& inst, uint64_t vpn, uint64_t pfn)
{
    auto func = [match_vpn=vpn, pfn] (Instruction::memop_list_t& v)
    {
        for (Memop& x : v) {
            if (x.state == AccessState::IN_TLB) {
                const auto [vpn, offset] = split_address<LINESIZE>(x.v_lineaddr);
                x.p_lineaddr = join_address<LINESIZE>(pfn, offset);
                x.state = AccessState::READY;
            }
        }
    };

    inst_do_func_dependent_on_state<AccessState::READY>(inst, func);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <TransactionType TTYPE> inline void
do_ldst(iptr_t& inst,
        uint8_t coreid,
        std::unique_ptr<L1DCache>& cache,
        Instruction::memop_state_array_t& st,
        Instruction::memop_list_t& v)
{
    inst_do_func_dependent_on_state<AccessState::IN_CACHE>(st, v, 
            [inst, coreid, c=cache.get()] (Instruction::memop_list_t& v)
            {
                for (Memop& x : v) {
                    if (x.state == AccessState::READY) {
                        Transaction trans(coreid, inst, TTYPE, x.p_lineaddr);
                        if (c->io_->add_incoming(trans))
                            x.state = AccessState::IN_CACHE;
                    }
                }
            });
}

void
inst_dcache_access(iptr_t& inst, uint8_t coreid, std::unique_ptr<L1DCache>& cache)
{
    do_ldst<TransactionType::READ>(inst, coreid, cache, inst->num_loads_in_state, inst->loads);
    do_ldst<TransactionType::WRITE>(inst, coreid, cache, inst->num_stores_in_state, inst->stores);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
drain_llc_outgoing_queue()
{
    drain_cache_outgoing_queue(GL_LLC,
            [] (const Transaction& t)
            {
                auto& core = GL_CORES[t.coreid];
                core->L2_->mark_load_as_done(t.address);
            });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
