#include "model/graph.hpp"

#include <algorithm>
#include <limits>
#include <queue>

#include "core/assertions.hpp"

namespace sim::graph {

namespace {
inline std::size_t ix(NodeId id) { return static_cast<std::size_t>(id); }
}

Adjacency build_adjacency(const std::vector<geo::Point>& pos, double radius, bool include_sink) {
    const auto n = pos.size();
    Adjacency adj(n);
    const std::size_t first = include_sink ? 0 : 1;
    for (std::size_t u = first; u < n; ++u)
        for (std::size_t v = u + 1; v < n; ++v)
            if (geo::within(pos[u], pos[v], radius)) {
                adj[u].push_back(static_cast<NodeId>(v));
                adj[v].push_back(static_cast<NodeId>(u));
            }
    // Lists are ascending by construction; assert rather than sort.
    for (const auto& l : adj) SIM_REQUIRE(std::is_sorted(l.begin(), l.end()), "adjacency list not ascending");
    return adj;
}

bool connected_to_sink(const Adjacency& adj) {
    std::vector<char> seen(adj.size(), 0);
    std::queue<NodeId> q;
    q.push(SINK);
    seen[ix(SINK)] = 1;
    std::size_t reached = 1;
    while (!q.empty()) {
        NodeId u = q.front(); q.pop();
        for (NodeId v : adj[ix(u)])
            if (!seen[ix(v)]) { seen[ix(v)] = 1; ++reached; q.push(v); }
    }
    return reached == adj.size();
}

std::vector<std::int32_t> greedy_colouring(const Adjacency& adj_gi, std::int32_t& K) {
    const auto n = adj_gi.size();
    std::vector<NodeId> order;
    for (std::size_t i = 1; i < n; ++i) order.push_back(static_cast<NodeId>(i));
    // non-increasing G_I degree, then ascending id
    std::stable_sort(order.begin(), order.end(), [&](NodeId a, NodeId b) {
        auto da = adj_gi[ix(a)].size(), db = adj_gi[ix(b)].size();
        if (da != db) return da > db;
        return a < b;
    });
    std::vector<std::int32_t> slot(n, 0);
    K = 0;
    std::vector<char> used;
    for (NodeId u : order) {
        used.assign(n + 2, 0);
        for (NodeId v : adj_gi[ix(u)]) {
            auto s = slot[ix(v)];
            if (s > 0) used[static_cast<std::size_t>(s)] = 1;
        }
        std::int32_t c = 1;
        while (used[static_cast<std::size_t>(c)]) ++c;
        slot[ix(u)] = c;
        K = std::max(K, c);
    }
    return slot;
}

std::int64_t colouring_violations(const Adjacency& adj_gi, const std::vector<std::int32_t>& slot) {
    std::int64_t bad = 0;
    for (std::size_t u = 1; u < adj_gi.size(); ++u)
        for (NodeId v : adj_gi[u])
            if (ix(v) > u && slot[u] == slot[ix(v)]) ++bad;
    return bad;
}

Tree minhop_tree(const std::vector<geo::Point>& pos, const Adjacency& adj_gc) {
    const auto n = pos.size();
    Tree t;
    t.parent.assign(n, NONE);
    t.depth.assign(n, -1);
    std::queue<NodeId> q;
    t.depth[ix(SINK)] = 0;
    q.push(SINK);
    while (!q.empty()) {
        NodeId u = q.front(); q.pop();
        for (NodeId v : adj_gc[ix(u)])
            if (t.depth[ix(v)] < 0) { t.depth[ix(v)] = t.depth[ix(u)] + 1; q.push(v); }
    }
    for (std::size_t i = 1; i < n; ++i) {
        SIM_REQUIRE(t.depth[i] > 0, "minhop_tree: node " << i << " unreachable; connectivity rejection failed");
        // nearest depth-(d-1) neighbour; `<` keeps the lowest id on ties
        NodeId best = NONE;
        double best_d2 = std::numeric_limits<double>::infinity();
        for (NodeId v : adj_gc[i]) {
            if (t.depth[ix(v)] != t.depth[i] - 1) continue;
            double d2 = geo::dist2(pos[i], pos[ix(v)]);
            if (d2 < best_d2) { best_d2 = d2; best = v; }
        }
        SIM_REQUIRE(best != NONE, "minhop_tree: node " << i << " has no depth-" << (t.depth[i] - 1) << " neighbour");
        t.parent[i] = best;
    }
    return t;
}

Tree eera_tree(const std::vector<geo::Point>& pos, const Adjacency& adj_gc) {
    const auto n = pos.size();
    Tree t;
    t.parent.assign(n, NONE);
    t.depth.assign(n, -1);
    std::vector<char> attached(n, 0);
    std::vector<double> best_d2(n, std::numeric_limits<double>::infinity());
    std::vector<NodeId> best_v(n, NONE);

    auto relax_from = [&](NodeId v) {
        for (NodeId u : adj_gc[ix(v)]) {
            if (attached[ix(u)]) continue;
            double d2 = geo::dist2(pos[ix(u)], pos[ix(v)]);
            // shorter edge, then lower id_v
            if (d2 < best_d2[ix(u)] || (d2 == best_d2[ix(u)] && v < best_v[ix(u)])) {
                best_d2[ix(u)] = d2;
                best_v[ix(u)] = v;
            }
        }
    };

    attached[ix(SINK)] = 1;
    t.depth[ix(SINK)] = 0;
    relax_from(SINK);
    for (std::size_t step = 1; step < n; ++step) {
        // unattached u minimising (d, id_u, id_v)
        NodeId pick = NONE;
        double pick_d2 = std::numeric_limits<double>::infinity();
        for (std::size_t u = 1; u < n; ++u)
            if (!attached[u] && best_v[u] != NONE && best_d2[u] < pick_d2) {
                pick_d2 = best_d2[u];
                pick = static_cast<NodeId>(u);
            }
        SIM_REQUIRE(pick != NONE, "eera_tree: no admissible edge at step " << step
                                  << "; connectivity rejection failed");
        attached[ix(pick)] = 1;
        t.parent[ix(pick)] = best_v[ix(pick)];
        t.depth[ix(pick)] = t.depth[ix(best_v[ix(pick)])] + 1;
        relax_from(pick);
    }
    return t;
}

std::vector<std::int32_t> tree_ordered_slots(const Tree& tree, std::int32_t& K) {
    const auto n = tree.parent.size();
    std::vector<NodeId> order;
    for (std::size_t i = 1; i < n; ++i) order.push_back(static_cast<NodeId>(i));
    // decreasing depth, then ascending id
    std::stable_sort(order.begin(), order.end(), [&](NodeId a, NodeId b) {
        auto da = tree.depth[ix(a)], db = tree.depth[ix(b)];
        if (da != db) return da > db;
        return a < b;
    });
    std::vector<std::int32_t> slot(n, 0);
    std::int32_t s = 0;
    for (NodeId u : order) slot[ix(u)] = ++s;
    K = s;
    return slot;
}

std::vector<std::int64_t> descendant_counts(const Tree& tree) {
    const auto n = tree.parent.size();
    std::vector<std::int64_t> desc(n, 0);
    // deepest first, so a count is final before it joins its parent's
    std::vector<NodeId> order;
    for (std::size_t i = 1; i < n; ++i) order.push_back(static_cast<NodeId>(i));
    std::stable_sort(order.begin(), order.end(), [&](NodeId a, NodeId b) {
        return tree.depth[ix(a)] > tree.depth[ix(b)];
    });
    for (NodeId u : order) {
        NodeId p = tree.parent[ix(u)];
        SIM_REQUIRE(p != NONE, "descendant_counts: node " << u << " has no parent");
        desc[ix(p)] += 1 + desc[ix(u)];
    }
    return desc;
}

double tree_total_length(const std::vector<geo::Point>& pos, const Tree& tree) {
    double total = 0.0;
    for (std::size_t i = 1; i < pos.size(); ++i) total += geo::dist(pos[i], pos[ix(tree.parent[i])]);
    return total;
}

std::int32_t tree_depth_max(const Tree& tree) {
    std::int32_t m = 0;
    for (auto d : tree.depth) m = std::max(m, d);
    return m;
}

}
