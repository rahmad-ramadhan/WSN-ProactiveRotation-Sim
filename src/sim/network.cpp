#include "sim/network.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/assertions.hpp"
#include "model/geometry.hpp"
#include "sim/variants.hpp"

namespace sim {

const char* stage_name(Stage s) {
    switch (s) {
        case Stage::Setup: return "setup";
        case Stage::S1: return "S1";
        case Stage::S2: return "S2";
        case Stage::S3: return "S3";
        case Stage::S4: return "S4";
        case Stage::S5: return "S5";
        case Stage::S6: return "S6";
        case Stage::S7: return "S7";
        case Stage::S8: return "S8";
        case Stage::Finished: return "finished";
    }
    return "?";
}

void AlgorithmContext::schedule_control(NodeId u, const ViewRecord& rec) {
    SIM_REQUIRE(stage == Stage::S3, "frame " << frame << ": arm scheduled a control message from stage "
                                             << stage_name(stage) << "; only S3 may emit");
    SIM_REQUIRE(u > SINK && static_cast<std::size_t>(u) < nodes.size(), "schedule_control: bad node id " << u);
    scheduled_.emplace_back(u, rec);
}

void AlgorithmContext::seed_view(NodeId u, const ViewRecord& rec) {
    SIM_REQUIRE(stage == Stage::Setup, "frame " << frame << ": seed_view called from stage " << stage_name(stage)
                                                << "; the initial dissemination is a setup step");
    SIM_REQUIRE(u > SINK && static_cast<std::size_t>(u) < nodes.size(), "seed_view: bad node id " << u);
    ViewRecord r = rec;
    r.frame = 0;
    for (NodeId v : nodes[static_cast<std::size_t>(u)].nbr) {
        if (v == SINK) continue;
        const std::int32_t idx = view_index(v, u);
        SIM_REQUIRE(idx >= 0, "seed_view: " << u << " is a neighbour of " << v << " but not the reverse");
        nodes[static_cast<std::size_t>(v)].view[static_cast<std::size_t>(idx)] = View{true, r};
    }
}

void AlgorithmContext::set_ctrl_due(NodeId i) {
    SIM_REQUIRE(stage == Stage::S5, "frame " << frame << ": ctrl_due(" << i << ") set from stage " << stage_name(stage)
                                             << "; only S5 may");
    SIM_REQUIRE(i > SINK && static_cast<std::size_t>(i) < nodes.size(), "set_ctrl_due: bad node id " << i);
    SIM_REQUIRE(nodes[static_cast<std::size_t>(i)].alive, "frame " << frame << ": dead node " << i << " marked ctrl_due");
    nodes[static_cast<std::size_t>(i)].ctrl_due = true;
}

void AlgorithmContext::set_parent(NodeId i, NodeId p) {
    SIM_REQUIRE(stage == Stage::S5 || stage == Stage::S6, "frame " << frame << ": par(" << i << ") written from stage "
                                                                   << stage_name(stage) << "; only S5 and S6 may");
    SIM_REQUIRE(i > SINK && static_cast<std::size_t>(i) < nodes.size(), "set_parent: bad node id " << i);
    SIM_REQUIRE(p == NONE || (p >= SINK && static_cast<std::size_t>(p) < nodes.size() && p != i),
                "set_parent: bad parent " << p << " for node " << i);
    Node& nd = nodes[static_cast<std::size_t>(i)];
    if (nd.parent == p) return;
    nd.parent = p;
    if (p == NONE) {
        ++nd.orphanings;
        ++counters.orphanings_frame;
        ++counters.orphanings_total;
        return;
    }
    nd.last_parent = p;
    ++nd.parent_changes;
    ++counters.parent_changes_frame;
    ++counters.parent_changes_total;
}

void AlgorithmContext::set_mtc(NodeId i, double mtc) {
    SIM_REQUIRE(stage == Stage::Setup || stage == Stage::S5 || stage == Stage::S6,
                "frame " << frame << ": MTC(" << i << ") written from stage " << stage_name(stage)
                         << "; only setup, S5 and S6 may");
    SIM_REQUIRE(i > SINK && static_cast<std::size_t>(i) < nodes.size(), "set_mtc: bad node id " << i);
    SIM_REQUIRE(!std::isnan(mtc), "frame " << frame << ": MTC(" << i << ") set to NaN (I12)");
    nodes[static_cast<std::size_t>(i)].MTC = mtc;
}

std::int64_t AlgorithmContext::inherit_slot(NodeId taker, NodeId dead) {
    SIM_REQUIRE(stage == Stage::S5, "frame " << frame << ": slot inheritance " << taker << " <- " << dead
                                             << " from stage " << stage_name(stage) << "; only S5 may");
    SIM_REQUIRE(net_ != nullptr, "inherit_slot: no loop attached");
    return net_->apply_slot_inheritance(taker, dead);
}

void AlgorithmContext::charge_ctrl_rx(NodeId v, double bits) {
    SIM_REQUIRE(stage == Stage::S3, "frame " << frame << ": receive charge for " << v << " from stage "
                                             << stage_name(stage) << "; only S3 may charge");
    SIM_REQUIRE(v > SINK && static_cast<std::size_t>(v) < nodes.size(), "charge_ctrl_rx: bad node id " << v);
    SIM_REQUIRE(nodes[static_cast<std::size_t>(v)].alive, "frame " << frame << ": dead node " << v << " charged a receive");
    SIM_REQUIRE(bits > 0.0, "frame " << frame << ": receive charge of " << bits << " bits for " << v);
    SIM_REQUIRE(net_ != nullptr, "charge_ctrl_rx: no loop attached");
    net_->charge_ctrl_rx(v, bits);
}

std::int32_t AlgorithmContext::live_chain(NodeId x) const {
    const auto N = static_cast<std::int32_t>(nodes.size()) - 1;
    std::int32_t steps = 0;
    while (x != SINK) {
        if (x == NONE || !nodes[static_cast<std::size_t>(x)].alive) return -1;
        x = nodes[static_cast<std::size_t>(x)].parent;
        if (++steps > N) return -1;
    }
    return steps;
}

std::vector<NodeId> AlgorithmContext::alive_children(NodeId p) const {
    std::vector<NodeId> out;
    for (std::size_t i = 1; i < nodes.size(); ++i)
        if (nodes[i].alive && nodes[i].parent == p) out.push_back(nodes[i].id);
    return out;
}

std::int32_t AlgorithmContext::view_index(NodeId i, NodeId j) const {
    const auto& nb = nodes[static_cast<std::size_t>(i)].nbr;
    auto it = std::lower_bound(nb.begin(), nb.end(), j);
    if (it == nb.end() || *it != j) return -1;
    return static_cast<std::int32_t>(it - nb.begin());
}

Network::Network(const Config& cfg, const Resolved& res, const Topology& topo, std::vector<Node>& nodes,
                 Algorithm& arm, const std::string& out_prefix, MetricsSink& sink)
    : cfg_(cfg), res_(res), topo_(topo), nodes_(nodes), arm_(arm), policy_(arm.invariant_policy()), sink_(sink),
      radio_{cfg.E_elec, cfg.eps_fs},
      ctrl_model_(variants::control_cost_model(cfg.control_cost_model)),
      ledger_(radio_, ctrl_model_, nodes.size(), cfg.l_data),
      ctx_(cfg, res, topo, nodes, precedence_rule(cfg.precedence_rule), counters_, sink, out_prefix),
      N_(cfg.N), l_(static_cast<double>(cfg.l_data)),
      frames_(out_prefix + "frames.csv"),
      trace_(out_prefix + "trace.csv", TracePolicy{cfg.trace_frames, cfg.trace_every, cfg.trace_min_routed}),
      nodes_out_(out_prefix + "nodes.csv"),
      charge_(nodes.size(), 0.0),
      E_snapshot_(nodes.size(), 0.0),
      i5_last_parent_(nodes.size(), NONE),
      slot_checksum_(topo.slot_checksum) {
    ctx_.net_ = this;
    rebuild_order();
    for (std::size_t i = 1; i < nodes_.size(); ++i) E_snapshot_[i] = nodes_[i].E;
}

void Network::charge_ctrl_rx(NodeId v, double bits) {
    ledger_.ctrl_rx(v, bits);
    ++ctrl_rx_only_f_;
}

// The taker gets the dead node's slot. New same-slot pairs among its alive G_I
// neighbours are counted and the slot checksum is re-based.
std::int64_t Network::apply_slot_inheritance(NodeId taker, NodeId dead) {
    SIM_REQUIRE(policy_.i6_counted, "frame " << f_ << ": slot inheritance " << taker << " <- " << dead
                                             << " requested by an arm that does not declare I6 counted; the only sanctioned slot write is nemcr's");
    SIM_REQUIRE(taker > SINK && static_cast<std::size_t>(taker) < nodes_.size() && dead > SINK
                    && static_cast<std::size_t>(dead) < nodes_.size() && taker != dead,
                "apply_slot_inheritance: bad ids " << taker << " <- " << dead);
    Node& t = nodes_[static_cast<std::size_t>(taker)];
    const Node& d = nodes_[static_cast<std::size_t>(dead)];
    SIM_REQUIRE(t.alive && !d.alive, "frame " << f_ << ": slot inheritance " << taker << " (alive=" << t.alive << ") <- "
                                              << dead << " (alive=" << d.alive << ")");
    t.slot = d.slot;
    rebuild_order();
    std::int64_t bad = 0;
    for (NodeId v : topo_.adj_gi[static_cast<std::size_t>(taker)]) {
        const Node& nv = nodes_[static_cast<std::size_t>(v)];
        if (nv.alive && nv.slot == t.slot) ++bad;
    }
    i6_violations_ += bad;
    ++slot_inheritances_;
    std::vector<std::int32_t> slot(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) slot[i] = nodes_[i].slot;
    slot_checksum_ = vector_checksum(slot);
    return bad;
}

double Network::dist(NodeId a, NodeId b) const {
    return geo::dist(topo_.pos[static_cast<std::size_t>(a)], topo_.pos[static_cast<std::size_t>(b)]);
}

// S2 order: ascending slot, then ascending id.
void Network::rebuild_order() {
    order_.clear();
    for (std::int64_t i = 1; i <= N_; ++i) order_.push_back(static_cast<NodeId>(i));
    std::stable_sort(order_.begin(), order_.end(), [&](NodeId a, NodeId b) {
        const Node& na = nodes_[static_cast<std::size_t>(a)];
        const Node& nb = nodes_[static_cast<std::size_t>(b)];
        if (na.slot != nb.slot) return na.slot < nb.slot;
        return a < b;
    });
}

void Network::run() {
    ctx_.stage = Stage::Setup;
    ctx_.frame = 0;
    arm_.init(ctx_);
    recompute_routes();

    std::string reason;
    for (f_ = 1;; ++f_) {
        ctx_.frame = f_;
        s1_sense();
        s2_forward();
        s3_control();
        s4_account();
        s5_react();
        s6_rotate();
        s7_measure();
        if (s8_stop(reason)) break;
    }
    ctx_.stage = Stage::Finished;
    arm_.finish(ctx_);
    for (std::size_t i = 1; i < nodes_.size(); ++i) if (nodes_[i].alive) nodes_out_.row(nodes_[i]);
    frames_.close();
    trace_.close();
    nodes_out_.close();
    publish(reason);
}

void Network::publish(const std::string& stop_reason) {
    std::int64_t held = 0, orphan_frames = 0, orphan_nodes = 0;
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        if (nodes_[i].alive) held += nodes_[i].B;
        orphan_frames += nodes_[i].frames_as_orphan;
        if (nodes_[i].frames_as_orphan > 0) ++orphan_nodes;
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();

    sink_.set_frame("fnd_frame", fnd_frame_);
    sink_.set_frame("t_1pct", t_1pct_);
    sink_.set_frame("t_10pct", t_10pct_);
    sink_.set_frame("t_50pct", t_50pct_);
    sink_.set_frame("partition_frame", partition_frame_);
    sink_.set_frame("adt_frame", adt_frame_);
    sink_.set_frame("t_unreach_25", t_unreach_25_);
    sink_.set_frame("t_unreach_50", t_unreach_50_);
    sink_.set_frame("t_unreach_75", t_unreach_75_);
    sink_.set("real_depth_max", static_cast<std::int64_t>(real_depth_max_));

    sink_.set("generated_total", gen_total_);
    sink_.set("delivered_total", deliv_total_);
    sink_.set("lost_orphan_total", lost_orphan_total_);
    sink_.set("lost_death_total", lost_death_total_);
    sink_.set("held_at_stop", held);
    sink_.set("pdr_global", gen_total_ > 0 ? static_cast<double>(deliv_total_) / static_cast<double>(gen_total_) : nan);
    sink_.set("pdr_to_fnd", fnd_frame_ ? pdr_to_fnd_ : nan);

    sink_.set("jain_index_fnd", fnd_frame_ ? jain_fnd_ : nan);
    sink_.set("jain_index_final", jain_consumption());
    sink_.set("energy_total", ledger_.total_issued());
    sink_.set("energy_distance_component", ledger_.total_distance_component());
    sink_.set("energy_stranded", energy_stranded_);
    sink_.set("energy_parentless", energy_parentless_);

    sink_.set("control_msgs", counters_.control_msgs);
    sink_.set("control_bits", counters_.control_bits);
    const std::int64_t total_bits = counters_.control_bits + ledger_.data_bits_total();
    sink_.set("control_bits_frac", total_bits > 0 ? static_cast<double>(counters_.control_bits) / static_cast<double>(total_bits) : 0.0);
    sink_.set("control_energy", ledger_.total_control_energy());

    sink_.set("parent_changes_total", counters_.parent_changes_total);
    sink_.set("orphanings_total", counters_.orphanings_total);
    sink_.set("orphan_frames_total", orphan_frames);
    sink_.set("orphan_nodes_ever", orphan_nodes);
    sink_.set("recovery_failures", recovery_failures_);
    sink_.set("static_order_violations", i5_violations_);
    sink_.set("slot_inheritances", slot_inheritances_);
    sink_.set("colouring_violations", i6_violations_);

    sink_.set("i14_frames_checked", i14_checked_);
    sink_.set("i16_max_B", i16_max_B_);
    sink_.set("i16_max_slot_bits", i16_max_slot_bits_);
    sink_.set("stop_frame", f_);
    sink_.set("stop_reason", stop_reason);
    sink_.set("trace_stop_reason", trace_.stop_reason());
    sink_.set("trace_frames_written", trace_.frames_written());
}

// Jain's index over cumulative consumption of all N sensors, dead included.
double Network::jain_consumption() const {
    double s = 0.0, ss = 0.0;
    for (std::size_t i = 1; i < nodes_.size(); ++i) { s += nodes_[i].E_cum; ss += nodes_[i].E_cum * nodes_[i].E_cum; }
    if (ss == 0.0) return 1.0;
    return (s * s) / (static_cast<double>(N_) * ss);
}

void Network::s1_sense() {
    ctx_.stage = Stage::S1;
    counters_.parent_changes_frame = 0;
    counters_.orphanings_frame = 0;
    gen_f_ = deliv_f_ = lost_orphan_f_ = lost_death_f_ = 0;
    energy_data_f_ = energy_ctrl_f_ = 0.0;
    ctrl_msgs_f_ = ctrl_rx_only_f_ = 0;
    max_B_f_ = max_slot_bits_f_ = 0;
    died_.clear();
    ctx_.died_.clear();
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        Node& nd = nodes_[i];
        if (!nd.alive) continue;
        nd.pending = 1;
        ++gen_f_;
    }
    gen_total_ += gen_f_;
}

void Network::s2_forward() {
    ctx_.stage = Stage::S2;
    for (std::size_t i = 1; i < nodes_.size(); ++i) { nodes_[i].sent = 0; nodes_[i].recv = 0; }

    std::int32_t cur_slot = -1;
    std::int64_t slot_bits = 0;
    auto close_slot = [&]() { max_slot_bits_f_ = std::max(max_slot_bits_f_, slot_bits); slot_bits = 0; };

    for (NodeId u : order_) {
        Node& nu = nodes_[static_cast<std::size_t>(u)];
        if (nu.slot != cur_slot) { close_slot(); cur_slot = nu.slot; }
        if (!nu.alive) continue;
        const std::int64_t count = nu.pending + nu.B;
        if (count == 0) continue;
        const double bits = l_ * static_cast<double>(count);
        slot_bits += static_cast<std::int64_t>(bits);
        max_B_f_ = std::max(max_B_f_, count);
        const NodeId p = nu.parent;
        if (p == SINK) {
            ledger_.data_tx(u, bits, dist(u, SINK));
            deliv_f_ += count;
        } else if (p != NONE && nodes_[static_cast<std::size_t>(p)].alive) {
            Node& np = nodes_[static_cast<std::size_t>(p)];
            ledger_.data_tx(u, bits, dist(u, p));
            ledger_.data_rx(p, bits);
            np.B += count;
            np.recv += count;
        } else {
            // Orphaned or parent dead: transmits at the last parent's distance; payloads are lost.
            const NodeId lp = nu.last_parent;
            SIM_REQUIRE(lp != NONE, "frame " << f_ << ": node " << u << " has no last parent for d_last");
            ledger_.data_tx(u, bits, dist(u, lp));
            lost_orphan_f_ += count;
            nu.payloads_lost_as_orphan += count;
        }
        nu.pending = 0;
        nu.B = 0;
        nu.sent = count;
    }
    close_slot();
    deliv_total_ += deliv_f_;
    lost_orphan_total_ += lost_orphan_f_;
    i16_max_B_ = std::max(i16_max_B_, max_B_f_);
    i16_max_slot_bits_ = std::max(i16_max_slot_bits_, max_slot_bits_f_);
}

void Network::s3_control() {
    ctx_.stage = Stage::S3;
    ctx_.scheduled_.clear();
    arm_.control(ctx_);
    auto& sched = ctx_.scheduled_;
    std::stable_sort(sched.begin(), sched.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t k = 0; k < sched.size(); ++k) {
        const NodeId u = sched[k].first;
        SIM_REQUIRE(k == 0 || sched[k - 1].first != u, "frame " << f_ << ": node " << u << " scheduled twice in S3");
        Node& nu = nodes_[static_cast<std::size_t>(u)];
        SIM_REQUIRE(nu.alive, "frame " << f_ << ": dead node " << u << " scheduled a control message");
        ViewRecord rec = sched[k].second;
        rec.frame = f_;
        const double bits = static_cast<double>(cfg_.l_ctrl);
        ledger_.ctrl_tx(u, bits, cfg_.R_max);
        for (NodeId v : nu.nbr) {
            if (v == SINK) continue;
            Node& nv = nodes_[static_cast<std::size_t>(v)];
            if (!nv.alive) continue;
            ledger_.ctrl_rx(v, bits);
            const std::int32_t idx = ctx_.view_index(v, u);
            SIM_REQUIRE(idx >= 0, "frame " << f_ << ": " << u << " is a neighbour of " << v << " but not the reverse");
            nv.view[static_cast<std::size_t>(idx)] = View{true, rec};
        }
        ++counters_.control_msgs;
        counters_.control_bits += cfg_.l_ctrl;
        ++ctrl_msgs_f_;
        nu.ctrl_due = false;
    }
    sched.clear();
}

void Network::s4_account() {
    ctx_.stage = Stage::S4;
    check_i9("S1-S3");
    SIM_INVARIANT("I15", ledger_.size(SINK) == 0, "frame " << f_ << ": the sink's ledger holds " << ledger_.size(SINK) << " entries");
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        Node& nd = nodes_[i];
        if (!nd.alive) {
            SIM_INVARIANT("I10", ledger_.size(nd.id) == 0, "frame " << f_ << ": dead node " << nd.id << " has ledger entries");
            charge_[i] = 0.0;
            continue;
        }
        energy::Settlement s = ledger_.settle(nd, cfg_.alpha);
        charge_[i] = s.charge;
        energy_data_f_ += s.data;
        energy_ctrl_f_ += s.ctrl;
        if (!nd.route) {
            const bool attached = nd.parent != NONE && nodes_[static_cast<std::size_t>(nd.parent)].alive;
            (attached ? energy_stranded_ : energy_parentless_) += s.data;
        }
        if (nd.E <= 0.0) {
            nd.alive = false;
            nd.death_frame = f_;
            died_.push_back(nd.id);
        }
    }
    deaths_total_ += static_cast<std::int64_t>(died_.size());

    // I1: N*E_0 == sum of residuals + ledger total.
    double residual = 0.0;
    for (std::size_t i = 1; i < nodes_.size(); ++i) residual += nodes_[i].E;
    const double budget = static_cast<double>(N_) * topo_.calib.E_0;
    const double rhs = residual + ledger_.total_issued();
    SIM_INVARIANT("I1", std::fabs(budget - rhs) <= 1e-9 * budget,
                  "frame " << f_ << ": N*E_0=" << budget << " residual+issued=" << rhs
                           << " rel=" << std::fabs(budget - rhs) / budget);

    for (std::size_t i = 1; i < nodes_.size(); ++i) E_snapshot_[i] = nodes_[i].E;

    if (!died_.empty()) {
        if (!fnd_frame_) {
            fnd_frame_ = f_;
            jain_fnd_ = jain_consumption();
            pdr_to_fnd_ = gen_total_ > 0 ? static_cast<double>(deliv_total_) / static_cast<double>(gen_total_) : 0.0;
        }
        auto threshold = [&](std::int64_t pct) { return (N_ * pct + 99) / 100; };   // ceil(N * pct / 100)
        if (!t_1pct_ && deaths_total_ >= threshold(1)) t_1pct_ = f_;
        if (!t_10pct_ && deaths_total_ >= threshold(10)) t_10pct_ = f_;
        if (!t_50pct_ && deaths_total_ >= threshold(50)) t_50pct_ = f_;
        if (!adt_frame_ && deaths_total_ >= N_) adt_frame_ = f_;
    }
}

void Network::s5_react() {
    ctx_.stage = Stage::S5;
    if (!died_.empty()) {
        for (NodeId i : died_) {
            Node& nd = nodes_[static_cast<std::size_t>(i)];
            lost_death_f_ += nd.B;
            nd.B = 0;
            nd.MTC = std::numeric_limits<double>::infinity();
            for (NodeId v : nd.nbr) {
                if (v == SINK) continue;
                const std::int32_t idx = ctx_.view_index(v, i);
                SIM_REQUIRE(idx >= 0, "frame " << f_ << ": " << i << " is a neighbour of " << v << " but not the reverse");
                nodes_[static_cast<std::size_t>(v)].view[static_cast<std::size_t>(idx)] = View{};
            }
        }
        lost_death_total_ += lost_death_f_;

        // Children still pointing at a dead parent after react() are orphaned.
        ctx_.died_ = died_;
        std::stable_sort(ctx_.died_.begin(), ctx_.died_.end(), [&](NodeId a, NodeId b) {
            const Node& na = nodes_[static_cast<std::size_t>(a)];
            const Node& nb = nodes_[static_cast<std::size_t>(b)];
            if (na.depth != nb.depth) return na.depth < nb.depth;
            return a < b;
        });
        for (NodeId p : ctx_.died_) {
            arm_.react(ctx_, p);
            for (std::size_t i = 1; i < nodes_.size(); ++i) {
                if (!nodes_[i].alive || nodes_[i].parent != p) continue;
                ctx_.set_parent(nodes_[i].id, NONE);
                ++recovery_failures_;
            }
        }
        last_topology_change_frame_ = f_;

        for (NodeId i : died_) nodes_out_.row(nodes_[static_cast<std::size_t>(i)]);
    }

    recompute_routes();
    real_depth_max_ = std::max(real_depth_max_, current_max_hop_);
    if (!partition_frame_ && routed_count_ == 0) partition_frame_ = f_;
    // First crossing only.
    {
        const std::int64_t unreachable = (N_ - alive_count_) + orphan_count_;
        auto threshold = [&](std::int64_t pct) { return (N_ * pct + 99) / 100; };
        if (!t_unreach_25_ && unreachable >= threshold(25)) t_unreach_25_ = f_;
        if (!t_unreach_50_ && unreachable >= threshold(50)) t_unreach_50_ = f_;
        if (!t_unreach_75_ && unreachable >= threshold(75)) t_unreach_75_ = f_;
    }

    // Orphan rule: alive, parent alive, no route.
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        const Node& nd = nodes_[i];
        if (!nd.alive || nd.route || nd.parent == NONE) continue;
        if (!nodes_[static_cast<std::size_t>(nd.parent)].alive) continue;
        arm_.orphan_rule(ctx_, nd.id);
    }

    check_tree("after S5");
}

void Network::recompute_routes() {
    alive_count_ = routed_count_ = orphan_count_ = 0;
    current_max_hop_ = 0;
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        Node& nd = nodes_[i];
        if (!nd.alive) { nd.route = false; continue; }
        ++alive_count_;
        NodeId x = nd.id;
        std::int32_t steps = 0;
        bool ok = true;
        while (x != SINK) {
            if (x == NONE || !nodes_[static_cast<std::size_t>(x)].alive) { ok = false; break; }
            x = nodes_[static_cast<std::size_t>(x)].parent;
            if (++steps > N_) { ok = false; break; }   // longer than N means a cycle
        }
        nd.route = ok;
        if (ok) { ++routed_count_; current_max_hop_ = std::max(current_max_hop_, steps); }
        else ++orphan_count_;
    }
}

void Network::check_tree(const char* where) {
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        const Node& nd = nodes_[i];
        if (!nd.alive) continue;
        const NodeId p = nd.parent;
        if (p == NONE) continue;
        const Node& np = nodes_[static_cast<std::size_t>(p)];
        SIM_INVARIANT("I4", p == SINK || np.alive,
                      "frame " << f_ << " " << where << ": node " << nd.id << " has dead parent " << p);
        SIM_INVARIANT("I4", geo::within(topo_.pos[i], topo_.pos[static_cast<std::size_t>(p)], cfg_.R_max),
                      "frame " << f_ << " " << where << ": link " << nd.id << " -> " << p << " is " << dist(nd.id, p) << " m > R_max");
        if (!ctx_.precedes(topo_.tree.depth, p, nd.id)) {
            // Count each violating link once, when it appears.
            if (policy_.i5_counted) { if (i5_last_parent_[i] != p) ++i5_violations_; }
            else SIM_INVARIANT("I5", false, "frame " << f_ << " " << where << ": parent " << p << " (dep " << np.depth
                                                     << ") does not precede " << nd.id << " (dep " << nd.depth << ")");
        }
        i5_last_parent_[i] = p;
        NodeId x = nd.id;
        std::int32_t steps = 0;
        while (x != SINK && x != NONE && nodes_[static_cast<std::size_t>(x)].alive) {
            x = nodes_[static_cast<std::size_t>(x)].parent;
            SIM_INVARIANT("I3", ++steps <= N_, "frame " << f_ << " " << where << ": parent chain from " << nd.id
                                                        << " exceeds " << N_ << " steps - a cycle");
        }
    }
}

void Network::check_i10() const {
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        const Node& nd = nodes_[i];
        if (nd.alive) continue;
        SIM_INVARIANT("I10", nd.B == 0 && nd.pending == 0,
                      "frame " << f_ << ": dead node " << nd.id << " holds B=" << nd.B << " pending=" << nd.pending);
        for (NodeId v : nd.nbr) {
            if (v == SINK) continue;
            const std::int32_t idx = ctx_.view_index(v, nd.id);
            SIM_INVARIANT("I10", idx >= 0 && !nodes_[static_cast<std::size_t>(v)].view[static_cast<std::size_t>(idx)].has,
                          "frame " << f_ << ": dead node " << nd.id << " still appears in the view of " << v);
        }
    }
}

void Network::s6_rotate() {
    ctx_.stage = Stage::S6;
    const std::int64_t before = counters_.parent_changes_frame + counters_.orphanings_frame;
    arm_.rotate(ctx_);
    if (counters_.parent_changes_frame + counters_.orphanings_frame > before) last_topology_change_frame_ = f_;
    check_tree("after S6");
}

void Network::check_i9(const char* where) const {
    if (f_ <= 1 && std::string(where) == "S1-S3") return;
    for (std::size_t i = 1; i < nodes_.size(); ++i)
        SIM_INVARIANT("I9", nodes_[i].E == E_snapshot_[i],
                      "frame " << f_ << ": E of node " << i << " moved across " << where << " from "
                               << E_snapshot_[i] << " to " << nodes_[i].E);
}

void Network::check_i14() {
    const bool costed_ctrl = ctrl_model_.charges && (ctrl_msgs_f_ > 0 || ctrl_rx_only_f_ > 0);
    const std::int64_t window = std::max<std::int64_t>(topo_.depth_max, current_max_hop_);
    const bool applicable = deaths_total_ == 0 && orphan_count_ == 0 && !costed_ctrl
                            && (f_ - last_topology_change_frame_) > window;
    if (!applicable) return;
    std::vector<std::int64_t> desc(nodes_.size(), 0);
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        NodeId x = nodes_[i].parent;
        while (x != SINK && x != NONE) { ++desc[static_cast<std::size_t>(x)]; x = nodes_[static_cast<std::size_t>(x)].parent; }
    }
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        const Node& nd = nodes_[i];
        const double m = 1.0 + static_cast<double>(desc[i]);
        const double expected = energy::E_tx(radio_, m * l_, dist(nd.id, nd.parent)) + energy::E_rx(radio_, (m - 1.0) * l_);
        SIM_INVARIANT("I14", std::fabs(charge_[i] - expected) <= 1e-12 * expected,
                      "frame " << f_ << ": node " << nd.id << " charge=" << charge_[i] << " expected=" << expected
                               << " m=" << m << " parent=" << nd.parent
                               << " rel=" << std::fabs(charge_[i] - expected) / expected);
    }
    ++i14_checked_;
}

void Network::s7_measure() {
    ctx_.stage = Stage::S7;
    check_i9("S5-S6");
    SIM_INVARIANT("I15", nodes_[0].parent == NONE, "frame " << f_ << ": the sink has parent " << nodes_[0].parent);
    check_i10();

    std::int64_t held = 0;
    for (std::size_t i = 1; i < nodes_.size(); ++i) if (nodes_[i].alive) held += nodes_[i].B;
    SIM_INVARIANT("I2", gen_total_ == deliv_total_ + lost_orphan_total_ + lost_death_total_ + held,
                  "frame " << f_ << ": generated=" << gen_total_ << " delivered=" << deliv_total_
                           << " lost_orphan=" << lost_orphan_total_ << " lost_death=" << lost_death_total_ << " held=" << held);

    if (f_ % 1000 == 0) {
        std::vector<std::int32_t> depth(nodes_.size()), slot(nodes_.size());
        for (std::size_t i = 0; i < nodes_.size(); ++i) { depth[i] = nodes_[i].depth; slot[i] = nodes_[i].slot; }
        SIM_INVARIANT("I7", vector_checksum(depth) == topo_.depth_checksum, "frame " << f_ << ": the depth vector changed");
        SIM_INVARIANT("I8", vector_checksum(slot) == slot_checksum_, "frame " << f_ << ": the slot vector changed outside a sanctioned inheritance");
    }

    check_i14();

    FrameRecord r;
    r.f = f_;
    r.alive = alive_count_;
    r.dead = N_ - alive_count_;
    r.orphan = orphan_count_;
    r.routed = routed_count_;
    r.delivered = deliv_f_;
    r.lost_orphan = lost_orphan_f_;
    r.lost_death = lost_death_f_;
    r.held_sum = held;
    r.energy_data = energy_data_f_;
    r.energy_ctrl = energy_ctrl_f_;
    r.parent_changes = counters_.parent_changes_frame;
    r.orphanings = counters_.orphanings_frame;
    r.max_B = max_B_f_;
    if (alive_count_ > 0) {
        double mn = 0.0, sum = 0.0;
        bool first = true;
        for (std::size_t i = 1; i < nodes_.size(); ++i) {
            if (!nodes_[i].alive) continue;
            if (first || nodes_[i].E < mn) mn = nodes_[i].E;
            first = false;
            sum += nodes_[i].E;
        }
        const double mean = sum / static_cast<double>(alive_count_);
        double ss = 0.0;
        for (std::size_t i = 1; i < nodes_.size(); ++i)
            if (nodes_[i].alive) { double d = nodes_[i].E - mean; ss += d * d; }
        r.residual_min = mn;
        r.residual_mean = mean;
        r.residual_sd = std::sqrt(ss / static_cast<double>(alive_count_));
    }
    frames_.write(r);

    for (std::size_t i = 1; i < nodes_.size(); ++i)
        if (nodes_[i].alive && !nodes_[i].route) ++nodes_[i].frames_as_orphan;

    if (trace_.begin_frame(f_, static_cast<double>(routed_count_) / static_cast<double>(N_)))
        for (std::size_t i = 1; i < nodes_.size(); ++i) trace_.row(f_, nodes_[i], charge_[i]);
}

// Reason priority: all_dead, partition, T_stop.
bool Network::s8_stop(std::string& reason) {
    ctx_.stage = Stage::S8;
    if (alive_count_ == 0) { reason = "all_dead"; return true; }
    if (routed_count_ == 0) { reason = "partition"; return true; }
    if (f_ >= cfg_.T_stop) { reason = "T_stop"; return true; }
    return false;
}

}
