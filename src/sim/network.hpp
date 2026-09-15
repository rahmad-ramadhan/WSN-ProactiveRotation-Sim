#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "arms/algorithm.hpp"
#include "core/config.hpp"
#include "core/node.hpp"
#include "io/metrics.hpp"
#include "io/nodes.hpp"
#include "io/trace.hpp"
#include "model/energy.hpp"
#include "sim/context.hpp"
#include "sim/setup.hpp"

namespace sim {

class Network {
public:
    Network(const Config& cfg, const Resolved& res, const Topology& topo, std::vector<Node>& nodes,
            Algorithm& arm, const std::string& out_prefix, MetricsSink& sink);

    void run();

private:
    friend class AlgorithmContext;
    const Config& cfg_;
    const Resolved& res_;
    const Topology& topo_;
    std::vector<Node>& nodes_;
    Algorithm& arm_;
    InvariantPolicy policy_;
    MetricsSink& sink_;

    energy::Radio radio_;
    const energy::ControlCostModel& ctrl_model_;
    energy::Ledger ledger_;
    Counters counters_;
    AlgorithmContext ctx_;

    std::int64_t N_;
    double l_;
    std::vector<NodeId> order_;               // ascending slot, then ascending id

    FramesWriter frames_;
    TraceWriter trace_;
    NodesWriter nodes_out_;

    std::int64_t f_ = 0;
    std::int64_t gen_f_ = 0, deliv_f_ = 0, lost_orphan_f_ = 0, lost_death_f_ = 0;
    double energy_data_f_ = 0.0, energy_ctrl_f_ = 0.0;
    // Data energy settled on alive nodes with no route: attached, or parentless.
    double energy_stranded_ = 0.0, energy_parentless_ = 0.0;
    std::int64_t ctrl_msgs_f_ = 0;
    std::int64_t ctrl_rx_only_f_ = 0;
    std::int64_t max_B_f_ = 0, max_slot_bits_f_ = 0;
    std::vector<double> charge_;
    std::vector<NodeId> died_;
    std::vector<double> E_snapshot_;
    std::int64_t alive_count_ = 0, routed_count_ = 0, orphan_count_ = 0;
    std::int32_t current_max_hop_ = 0;

    std::int64_t gen_total_ = 0, deliv_total_ = 0, lost_orphan_total_ = 0, lost_death_total_ = 0;
    std::int64_t deaths_total_ = 0;
    std::int64_t i14_checked_ = 0;
    std::int64_t i16_max_B_ = 0, i16_max_slot_bits_ = 0;
    std::int64_t recovery_failures_ = 0;
    std::int64_t i5_violations_ = 0;
    std::vector<NodeId> i5_last_parent_;
    std::int64_t slot_inheritances_ = 0;
    std::int64_t i6_violations_ = 0;
    std::int32_t real_depth_max_ = 0;
    std::int64_t last_topology_change_frame_ = 0;
    std::uint64_t slot_checksum_;

    // nullopt = not reached
    std::optional<std::int64_t> fnd_frame_, t_1pct_, t_10pct_, t_50pct_, partition_frame_, adt_frame_;
    std::optional<std::int64_t> t_unreach_25_, t_unreach_50_, t_unreach_75_;
    double jain_fnd_ = 0.0, pdr_to_fnd_ = 0.0;

    void s1_sense();
    void s2_forward();
    void s3_control();
    void s4_account();
    void s5_react();
    void s6_rotate();
    void s7_measure();
    bool s8_stop(std::string& reason);

    double dist(NodeId a, NodeId b) const;
    void rebuild_order();
    std::int64_t apply_slot_inheritance(NodeId taker, NodeId dead);
    void charge_ctrl_rx(NodeId v, double bits);
    void recompute_routes();
    void check_tree(const char* where);
    void check_i10() const;
    void check_i14();
    void check_i9(const char* where) const;
    double jain_consumption() const;
    void publish(const std::string& stop_reason);
};

}
