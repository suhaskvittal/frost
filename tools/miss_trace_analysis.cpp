/*
 *  author: Suhas Vittal
 *  date:   17 February 2025
 * */

#include "util/stats.h"

#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <iomanip>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct edge_data;

struct node_type
{
    using value_type = uint64_t;
    using edge_ptr = edge_data*;
    using edge_array = std::vector<edge_ptr>;

    edge_array f_adj_list{};
    edge_array b_adj_list{};

    std::vector<uint64_t> last_evictors{};

    ~node_type(void);

    void add_edge(edge_ptr, bool forward);
};

using node_array = std::unordered_map<uint64_t, node_type>;
using node_iterator = node_array::iterator;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct edge_data
{
    const node_iterator src;
    const node_iterator dst;

    int multiplicity =1;

    edge_data(node_iterator s, node_iterator d)
        :src(s),
        dst(d)
    {}
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

inline
node_type::~node_type(void)
{
    for (edge_data* e : f_adj_list)
        delete e;
}

inline void 
node_type::add_edge(edge_ptr e, bool forward)
{
    auto& alist = forward ? f_adj_list : b_adj_list;
    alist.push_back(e);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Simple (di)graph implementation: for us, memory is a problem,
 * so this needs to be very small.
 * */
struct Graph
{
    node_array nodes;
    /*
     * Other important metadata:
     * */
    using node_iterator_pair = std::pair<node_iterator, node_iterator>;

    size_t total_misses =0;
    size_t number_of_edges =0;
    size_t num_immd_multiplicity =0;
    size_t num_immd_exact_multiplicity =0;
    size_t num_immd_partial_multiplicity =0;
    /*
     * Graph functions:
     * */
    void add_edge(uint64_t, uint64_t, bool partial);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Graph::add_edge(uint64_t src, uint64_t dst, bool partial)
{
    if (!partial)
        ++total_misses;

    // Find source and destination:
    auto src_it = nodes.find(src),
         dst_it = nodes.find(dst);

    bool src_not_found = src_it == nodes.end(),
         dst_not_found = dst_it == nodes.end();

    // If either `src` or `dst` are not currently a node, make them:
    if (src_not_found)
        nodes.insert({ src, node_type{} });

    if (dst_not_found)
        nodes.insert({ dst, node_type{} });

    // We need to recompute `src_it` and `dst_it`
    if (src_not_found)
        src_it = nodes.find(src);
    if (dst_not_found)
        dst_it = nodes.find(dst);

    auto& src_node = src_it->second;

    edge_data* e;

    // Check if the edge already exists:
    auto e_it = std::find_if(src_node.f_adj_list.begin(), src_node.f_adj_list.end(),
                        [dst_it] (const auto* e) { return e->dst == dst_it; });

    if (e_it != src_node.f_adj_list.end())
    {
        e = *e_it;
        ++e->multiplicity;
        
        auto ev_it = std::find(src_node.last_evictors.begin(), src_node.last_evictors.end(), dst_it->first);
        if (ev_it != src_node.last_evictors.end())
        {
            // Exact multiplicity: evictor was the same last time and this time
            // Partial multiplicity: last evictor is amongst the nearby LRU ways this time
            if (ev_it == src_node.last_evictors.begin())
            {
                if (partial)
                    ++num_immd_partial_multiplicity;
                else
                    ++num_immd_exact_multiplicity;
                ++num_immd_multiplicity;
            }
        }
    }
    else
    {
        e = new edge_data(src_it, dst_it);
    
        src_it->second.add_edge(e, true);
        dst_it->second.add_edge(e, false);

        ++number_of_edges;
    }

    // Update src node last evictors:
    if (partial)
        src_node.last_evictors.push_back(dst_it->first);
    else
        src_node.last_evictors = {dst_it->first};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    gzFile fin = gzopen(argv[1], "r");
    uint64_t max_reads = atoll(argv[2]);

    // Read data into graph:
    Graph GR;

    std::cout << "progress:\t";
    std::cout.flush();

    while (!gzeof(fin) && max_reads--)
    {
        if (max_reads % 100000 == 0)
        {
            std::cout << ".";
            std::cout.flush();
        }

        uint32_t num_ways;
        uint64_t installed;

        gzread(fin, &num_ways, 4);
        std::vector<uint64_t> lru_ways(num_ways);

        for (size_t i = 0; i < num_ways; i++)
            gzread(fin, &lru_ways[i], 8);
        gzread(fin, &installed, 8);

        for (size_t i = 0; i < num_ways; i++)
            GR.add_edge(lru_ways[i], installed, i != 0);
    }
    std::cout << "\n";

    gzclose(fin);

    // Print stats:
    print_stat(std::cout, "WORKLOAD", "TOTAL_MISSES", GR.total_misses);
    print_stat(std::cout, "GRAPH", "NUMBER_OF_EDGES", GR.number_of_edges);
    print_stat(std::cout, "GRAPH", "IMMEDIATE_MULTIPLICITY", GR.num_immd_multiplicity);
    print_stat(std::cout, "GRAPH", "IMMEDIATE_EXACT_MULTIPLICITY", GR.num_immd_exact_multiplicity);
    print_stat(std::cout, "GRAPH", "IMMEDIATE_PARTIAL_MULTIPLICITY", GR.num_immd_partial_multiplicity);

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
