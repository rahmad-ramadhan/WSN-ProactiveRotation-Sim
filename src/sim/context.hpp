#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/config.hpp"
#include "core/node.hpp"
#include "io/metrics.hpp"
#include "sim/precedence.hpp"
#include "sim/setup.hpp"

namespace sim {

enum class Stage { Setup, S1, S2, S3, S4, S5, S6, S7, S8, Finished };
const char* stage_name(Stage s);

struct Counters {
    std::int64_t control_msgs = 0;
    std::int64_t control_bits = 0;
    std::int64_t parent_changes_frame = 0;
    std::int64_t parent_changes_total = 0;
    std::int64_t orphanings_frame = 0;
    std::int64_t orphanings_total = 0;
};

class Network;

class AlgorithmContext {
public:
    AlgorithmContext(const Config& cfg, const Resolved& res, const Topology& topo, std::vector<Node>& nodes,
                     PrecedesFn precedes, Counters& counters, MetricsSink& sink, std::string out_prefix)
        : cfg(cfg), res(res), topo(topo), nodes(nodes), precedes(precedes), counters(counters), sink(sink),
          out_prefix(std::move(out_prefix)) {}

    const Config& cfg;
    const Resolved& res;
    const Topology& topo;
    std::vector<Node>& nodes;
    PrecedesFn precedes;            // cycle-freedom order
    Counters& counters;
    MetricsSink& sink;
    std::string out_prefix;         // "<out>/"

    std::int64_t frame = 0;
    Stage stage = Stage::Setup;

    // S3 only.
    void schedule_control(NodeId u, const ViewRecord& rec);

    // S3 only. A receive-only charge: no view or counter changes.
    void charge_ctrl_rx(NodeId v, double bits);

    // Setup only. Free initial dissemination to every sensor neighbour.
    void seed_view(NodeId u, const ViewRecord& rec);

    // S5 only. The node sends at the next S3.
    void set_ctrl_due(NodeId i);

    // S5 / S6 only. Counts switches and orphanings.
    void set_parent(NodeId i, NodeId p);

    // Setup / S5 / S6 only. +inf means no route; NaN is rejected.
    void set_mtc(NodeId i, double mtc);

    // S5 only, for arms that count I6. Returns the new same-slot pairs.
    std::int64_t inherit_slot(NodeId taker, NodeId dead);

    // Hops to the sink through alive parents on the current tree, or -1.
    std::int32_t live_chain(NodeId x) const;

    // Ascending depth, then id. Valid during S5.
    const std::vector<NodeId>& died_this_frame() const { return died_; }

    std::vector<NodeId> alive_children(NodeId p) const;

    // -1 if j is not a neighbour of i.
    std::int32_t view_index(NodeId i, NodeId j) const;

private:
    friend class Network;
    Network* net_ = nullptr;
    std::vector<std::pair<NodeId, ViewRecord>> scheduled_;
    std::vector<NodeId> died_;
};

}
