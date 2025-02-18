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
#include <unordered_map>
#include <memory>
#include <vector>

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

    size_t indegree =0;
    size_t outdegree =0;

    bool has_multiplicity =false;

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
    const uint32_t s_count;

    edge_data(node_iterator s, node_iterator d, uint32_t sc)
        :src(s),
        dst(d),
        s_count(sc)
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
    auto& deg = forward ? outdegree : indegree;

    alist.push_back(e);
    ++deg;
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

    size_t number_of_edges =0;
    size_t max_indegree =0;
    size_t max_outdegree =0;
    size_t max_multiplicity =0;
    size_t num_immd_multiplicity =0;
    /*
     * Graph functions:
     * */
    void add_edge(uint64_t, uint64_t, uint32_t s_count);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Graph::add_edge(uint64_t src, uint64_t dst, uint32_t s_count)
{
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

    // Make the edge:
    edge_data* e = new edge_data(src_it, dst_it, s_count);
    src_it->second.add_edge(e, true);
    dst_it->second.add_edge(e, false);

    // Compute multiplicity:
    size_t multiplicity = 0;
    if (!src_not_found && !dst_not_found)
    {
        const auto& f_adj_list = src_it->second.f_adj_list;

        // Then `src` and `dst` might already be connected -- check for this:
        multiplicity = std::count_if(f_adj_list.begin(), f_adj_list.end(),
                                            [&dst_it] (const edge_data* e) { return e->dst == dst_it; });
        src_it->second.has_multiplicity = multiplicity > 0;

        // Check if this is an immediate multiplicity:
        if (f_adj_list.size() >= 2)
        {
            bool immediate_multiplicity = f_adj_list.at(f_adj_list.size()-2)->dst == dst_it;
            if (immediate_multiplicity)
                ++num_immd_multiplicity;
        }
    }

    // Update stats:
    ++number_of_edges;

    max_indegree = std::max(max_indegree, std::max(src_it->second.indegree, dst_it->second.indegree));
    max_outdegree = std::max(max_outdegree, std::max(src_it->second.outdegree, dst_it->second.outdegree));
    max_multiplicity = std::max(max_multiplicity, multiplicity);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    FILE* fin = fopen(argv[1], "r");
    uint64_t max_reads = atoll(argv[2]);

    // Read data into graph:
    Graph GR;

    uint64_t src;
    uint64_t dst;
    uint32_t s_count;

    std::cout << "progress:\t";
    std::cout.flush();

    while (!feof(fin) && max_reads--)
    {
        if (max_reads % 100000 == 0)
        {
            std::cout << ".";
            std::cout.flush();
        }

        fread(&s_count, 4, 1, fin);
        fread(&src, 8, 1, fin);
        fread(&dst, 8, 1, fin);

        // Create graph edge:
        GR.add_edge(src, dst, s_count);
    }
    std::cout << "\n";

    fclose(fin);

    // Now print stats about the graph we are interested in:
    const size_t node_count = GR.nodes.size();
    const size_t edge_count = GR.number_of_edges;

    const size_t tot_multiplicity = std::count_if(GR.nodes.begin(), GR.nodes.end(),
                                        [] (const auto& p) { return p.second.has_multiplicity; });

    print_stat(std::cout, "GRAPH", "NODE_COUNT", node_count);
    print_stat(std::cout, "GRAPH", "EDGE_COUNT", edge_count);
    print_stat(std::cout, "GRAPH", "MAX_INDEGREE", GR.max_indegree);
    print_stat(std::cout, "GRAPH", "MAX_OUTDEGREE", GR.max_outdegree);
    print_stat(std::cout, "GRAPH", "MAX_MULTIPLICITY", GR.max_multiplicity);
    print_stat(std::cout, "GRAPH", "NODES_WITH_MULTIPLICITY", tot_multiplicity);
    print_stat(std::cout, "GRAPH", "IMMEDIATE_MULTIPLICITY", GR.num_immd_multiplicity);

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
