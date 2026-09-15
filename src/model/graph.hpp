#pragma once

#include <cstdint>
#include <vector>

#include "core/node.hpp"
#include "model/geometry.hpp"

namespace sim::graph {

using Adjacency = std::vector<std::vector<NodeId>>;   // index = node id, lists ascending

// Edge iff d(u,v) <= radius. Without the sink, entry 0 is empty.
Adjacency build_adjacency(const std::vector<geo::Point>& pos, double radius, bool include_sink);

bool connected_to_sink(const Adjacency& adj);

// Visit by non-increasing degree, ties by id; take the smallest free colour. K = colours used.
std::vector<std::int32_t> greedy_colouring(const Adjacency& adj_gi, std::int32_t& K);

std::int64_t colouring_violations(const Adjacency& adj_gi, const std::vector<std::int32_t>& slot);

struct Tree {
    std::vector<NodeId> parent;
    std::vector<std::int32_t> depth;
};

// BFS from the sink; parent is the nearest depth-(d-1) neighbour, ties by id.
Tree minhop_tree(const std::vector<geo::Point>& pos, const Adjacency& adj_gc);

// Prim from the sink; shortest edge first, ties by id_u then id_v.
Tree eera_tree(const std::vector<geo::Point>& pos, const Adjacency& adj_gc);

// Deepest first, ties by id, slots 1..N. K = N.
std::vector<std::int32_t> tree_ordered_slots(const Tree& tree, std::int32_t& K);

std::vector<std::int64_t> descendant_counts(const Tree& tree);

double tree_total_length(const std::vector<geo::Point>& pos, const Tree& tree);

std::int32_t tree_depth_max(const Tree& tree);

}
