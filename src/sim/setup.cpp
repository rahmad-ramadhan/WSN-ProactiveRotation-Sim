#include "sim/setup.hpp"

#include <algorithm>

#include "core/assertions.hpp"
#include "core/format.hpp"
#include "model/deployment.hpp"
#include "sim/precedence.hpp"
#include "sim/variants.hpp"

namespace sim {

std::uint64_t vector_checksum(const std::vector<std::int32_t>& v) {
    std::string bytes;
    bytes.reserve(v.size() * 4);
    for (auto x : v) {
        auto u = static_cast<std::uint32_t>(x);
        for (int b = 0; b < 4; ++b) bytes.push_back(static_cast<char>((u >> (8 * b)) & 0xFF));
    }
    return fmt::fnv1a64(bytes);
}

Topology build_topology(const Config& cfg, const Resolved& res, RngService& rng, std::vector<Node>& nodes) {
    Topology t;
    const auto n = static_cast<std::size_t>(cfg.N) + 1;

    deploy::Deployment dep = cfg.positions.empty()
        ? deploy::draw_deployment(rng, cfg.rho, cfg.seed, cfg.N, res.L, cfg.R_max)
        : deploy::read_deployment(cfg.positions, cfg.N, res.L, cfg.R_max);
    t.pos = std::move(dep.pos);
    t.rejections = dep.rejections;
    t.rng_seed_used = dep.rng_seed_used;
    t.positions_source = dep.source;

    t.adj_gc = graph::build_adjacency(t.pos, cfg.R_max, true);
    t.adj_gi = graph::build_adjacency(t.pos, cfg.R_int, false);
    SIM_REQUIRE(graph::connected_to_sink(t.adj_gc), "setup: accepted deployment is not connected");
    std::size_t deg_sum = 0;
    for (const auto& l : t.adj_gc) deg_sum += l.size();
    t.mean_degree_gc = static_cast<double>(deg_sum) / static_cast<double>(n);
    for (std::size_t i = 1; i < n; ++i)
        t.max_degree_gi = std::max(t.max_degree_gi, static_cast<std::int32_t>(t.adj_gi[i].size()));

    const variants::TopologyProfile& profile = variants::topology_profile(cfg.topology_profile, cfg.tree_rule);
    t.profile = profile.name;
    t.tree = profile.tree(t.pos, t.adj_gc);
    t.slot = profile.slots(t.tree, t.adj_gi, t.K);
    t.depth_max = graph::tree_depth_max(t.tree);
    t.desc = graph::descendant_counts(t.tree);
    t.tree_total_length = graph::tree_total_length(t.pos, t.tree);

    PrecedesFn precedes = precedence_rule(cfg.precedence_rule);
    // Under `paper` slots are unique, so this is trivially 0.
    t.colouring_violations = graph::colouring_violations(t.adj_gi, t.slot);
    SIM_INVARIANT("I6", t.colouring_violations == 0,
                  "setup colouring has " << t.colouring_violations << " same-slot pairs within R_int");
    for (std::size_t i = 1; i < n; ++i) {
        NodeId p = t.tree.parent[i];
        SIM_INVARIANT("I4", p != NONE && geo::within(t.pos[i], t.pos[static_cast<std::size_t>(p)], cfg.R_max),
                      "setup link " << i << " -> " << p << " exceeds R_max");
        SIM_INVARIANT("I5", precedes(t.tree.depth, p, static_cast<NodeId>(i)),
                      "setup parent " << p << " does not precede " << i << " under precedence_rule=" << cfg.precedence_rule);
        SIM_INVARIANT("I8", t.slot[i] >= 1 && t.slot[i] <= t.K, "slot of " << i << " = " << t.slot[i] << " outside 1..K");
    }
    SIM_INVARIANT("I15", t.tree.parent[0] == NONE && t.slot[0] == 0 && t.tree.depth[0] == 0,
                  "sink has parent/slot/depth " << t.tree.parent[0] << "/" << t.slot[0] << "/" << t.tree.depth[0]);
    if (profile.slots_unique) {
        std::vector<std::int32_t> s(t.slot.begin() + 1, t.slot.end());
        std::sort(s.begin(), s.end());
        for (std::size_t i = 0; i < s.size(); ++i)
            SIM_INVARIANT("I8", s[i] == static_cast<std::int32_t>(i) + 1, "paper slots are not a permutation of 1..N");
    }

    energy::Radio radio{cfg.E_elec, cfg.eps_fs};
    t.calib = calib::calibrate(t.pos, t.tree, t.desc, radio, static_cast<double>(cfg.l_data),
                               cfg.kappa, static_cast<double>(cfg.T_run));

    t.depth_checksum = vector_checksum(t.tree.depth);
    t.slot_checksum = vector_checksum(t.slot);

    nodes.assign(n, Node{});
    for (std::size_t i = 0; i < n; ++i) {
        Node& nd = nodes[i];
        nd.id = static_cast<NodeId>(i);
        nd.x = t.pos[i].x;
        nd.y = t.pos[i].y;
        nd.slot = t.slot[i];
        nd.depth = t.tree.depth[i];
        nd.nbr = t.adj_gc[i];
        nd.view.assign(nd.nbr.size(), View{});
        nd.parent = t.tree.parent[i];
        nd.last_parent = t.tree.parent[i];
        nd.alive = true;
        nd.route = true;
        nd.E = t.calib.E_0;
        nd.P_hat = t.calib.P[i];
        nd.CES = t.calib.P[i];
        nd.MES = t.calib.P[i];
    }
    return t;
}

}
