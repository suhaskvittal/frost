/*
 *  author: Suhas Vittal
 *  date:   24 January 2025
 * */

#include <cstdint>

uint64_t GL_CYCLE = 0;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "cache.h"
#include "trace/fmt.h"
#include "trace/reader.h"
#include "util/numerics.h"
#include "util/stats.h"

#include <memory>
#include <optional>
#include <vector>

#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr CacheReplPolicy COMMON_REPL = CacheReplPolicy::LRU;

constexpr size_t PAGESIZE = 4096;
constexpr size_t LINESIZE = 64;

constexpr size_t L1I_SIZE_KB = 32;
constexpr size_t L1D_SIZE_KB = 48;

constexpr size_t L2_SIZE_KB = 512;
constexpr size_t L2_ASSOC = 8;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t L1_SETS = PAGESIZE / LINESIZE;

constexpr size_t l1_assoc(size_t size_kb)
{
    return ((size_kb*1024) / (L1_SETS*LINESIZE));
}

constexpr size_t sets(size_t size_kb, size_t assoc)
{
    return (size_kb*1024) / (assoc*LINESIZE);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using L1ICache = Cache<L1_SETS, l1_assoc(L1I_SIZE_KB), COMMON_REPL>;
using L1DCache = Cache<L1_SETS, l1_assoc(L1D_SIZE_KB), COMMON_REPL>;
using L2Cache = Cache<sets(L2_SIZE_KB, L2_ASSOC), L2_ASSOC, COMMON_REPL>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct L2MissData
{
    uint64_t address;
    uint64_t victim;
    bool victim_is_dirty =false;
};

using l2_miss_output_type = std::optional<L2MissData>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class UPPER_CACHE, class LOWER_CACHE> l2_miss_output_type
probe_and_install_on_miss(
        std::unique_ptr<UPPER_CACHE>& c1,
        std::unique_ptr<LOWER_CACHE>& c2, 
        uint64_t address,
        bool is_write)
{
    l2_miss_output_type out;

    if (c1->probe(address, is_write))
        return out;
    // Check `c2`.
    if (c2->probe(address))
    {
        auto victim = c1->fill(address, 1, is_write);
        if (victim.has_value() && victim.value().dirty)
            c2->mark(victim.value().address, true);
        return out;
    }
    // Otherwise, we have a LLC access: 
    L2MissData d;
    d.address = address;

    auto victim = c2->fill(address, 1, false);
    if (victim.has_value())
    {
        d.victim = victim.value().address;
        d.victim_is_dirty = victim.value().dirty;
    }
    out.emplace(std::move(d));
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
write_to_mtfout(gzFile& mtfout, const l2_miss_output_type& out, uint64_t inst_num)
{
    if (out.has_value())
    {
        const L2MissData& m = out.value();
        gzwrite(mtfout, &inst_num, 5);
        gzputc(mtfout, 0);
        gzwrite(mtfout, &m.address, 4);
        if (m.victim_is_dirty)
        {
            gzwrite(mtfout, &inst_num, 5);
            gzputc(mtfout, 1);
            gzwrite(mtfout, &m.victim, 4);
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t IF_BUFFER_SIZE = 32;

using if_buffer_type = Cache<1, IF_BUFFER_SIZE, CacheReplPolicy::LRU>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using champsim_reader_type = TraceReader<ChampsimTraceFormat>;
using l1i_ptr = std::unique_ptr<L1ICache>;
using l1d_ptr = std::unique_ptr<L1DCache>;
using l2_ptr = std::unique_ptr<L2Cache>;

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: ./port_champsim_to_mtf <champsim-trace-path> <mtf-trace-path>\n";
        return 1;
    }

    std::string input_trace(argv[1]);

    champsim_reader_type csreader(input_trace);
    gzFile mtfout = gzopen(argv[2], "w");
    /*
     * Initialize caches:
     * */
    l1i_ptr l1i(new L1ICache);
    l1d_ptr l1d(new L1DCache);
    l2_ptr l2(new L2Cache);

    if_buffer_type if_buffer;

    uint64_t inst_num = 0;
    while (!csreader.eof_)
    {
        if (inst_num % 10'000'000 == 0)
            std::cout << "[ status ] instruction number " << inst_num << "\n";

        std::vector<uint64_t> loads, stores;

        // Get trace data:
        auto& b = csreader();
        std::remove_copy(std::begin(b.src_mem), std::end(b.src_mem), std::back_inserter(loads), 0);
        std::remove_copy(std::begin(b.dst_mem), std::end(b.dst_mem), std::back_inserter(stores), 0);
        
        // Convert data to line addresses:
        for (uint64_t& x : loads)
            x >>= numeric_traits<LINESIZE>::log2;
        for (uint64_t& x : stores)
            x >>= numeric_traits<LINESIZE>::log2;

        uint64_t ip = b.ip >> numeric_traits<LINESIZE>::log2;
        if (!if_buffer.probe(ip))
        {
            // Do L1i$ access and update `if_buffer`
            write_to_mtfout(mtfout, probe_and_install_on_miss(l1i, l2, ip, false), inst_num);
            // Install `ip` into `if_buffer`
            if_buffer.fill(ip, 1, false);
        }
        // Perform data cache accesses:
        for (uint64_t x : loads)
            write_to_mtfout(mtfout, probe_and_install_on_miss(l1d, l2, x, false), inst_num);
        for (uint64_t x : stores)
            write_to_mtfout(mtfout, probe_and_install_on_miss(l1d, l2, x, true), inst_num);
        
        ++inst_num;
        ++GL_CYCLE;
    }
    gzclose(mtfout);
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
