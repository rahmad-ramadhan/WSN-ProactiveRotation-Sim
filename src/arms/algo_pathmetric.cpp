// fa: Chang & Tassiulas 2000 flow augmentation.
// Hop cost e_ij^x1 * (E/E_0)^(-x2), summed along the path.
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "arms/algorithm.hpp"
#include "core/assertions.hpp"
#include "core/config.hpp"
#include "model/energy.hpp"
#include "model/geometry.hpp"
#include "model/liu_cost.hpp"
#include "sim/context.hpp"
#include "sim/neighborview.hpp"
#include "sim/variants.hpp"

namespace sim {

namespace {

constexpr double TIE_EPS = 1e-12;
constexpr double INF = std::numeric_limits<double>::infinity();

class PathMetricArm final : public Algorithm {
public:
    explicit PathMetricArm(const Config& cfg)
        : attribution_(variants::dcfr_cost_attribution(cfg.dcfr_cost_attribution)),
          guards_{cfg.dcfr_sin_eps, cfg.dcfr_cost_cap},
          period_(variants::dcfr_period(cfg.pm_period)(cfg.M)),
          mode_(variants::knowledge_mode(cfg.knowledge_mode)),
          radio_{cfg.E_elec, cfg.eps_fs},
          l_(static_cast<double>(cfg.l_data)),
          R_max_(cfg.R_max),
          x1_(cfg.fa_x1), x2_(cfg.fa_x2) {
        SIM_REQUIRE(period_ >= 1, "pm_period resolved to " << period_ << " frames; must be >= 1");
    }

    void init(AlgorithmContext& ctx) override {
        const std::size_t n = ctx.nodes.size();
        E_0_ = ctx.topo.calib.E_0;
        aux_.assign(n, 0.0);
        view_ = std::make_unique<NeighborView>(mode_, ctx);

        for (std::size_t i = 1; i < n; ++i) ctx.seed_view(ctx.nodes[i].id, record_of(ctx, ctx.nodes[i].id));
        snapshot(ctx);
        std::vector<NodeId> order;
        for (std::size_t i = 1; i < n; ++i) order.push_back(ctx.nodes[i].id);
        std::stable_sort(order.begin(), order.end(), [&](NodeId a, NodeId b) {
            const auto da = ctx.nodes[static_cast<std::size_t>(a)].depth, db = ctx.nodes[static_cast<std::size_t>(b)].depth;
            if (da != db) return da < db;
            return a < b;
        });
        for (NodeId i : order) {
            const NodeId p = ctx.nodes[static_cast<std::size_t>(i)].parent;
            SIM_REQUIRE(p != NONE, "setup: node " << i << " has no initial parent");
            ViewRecord rp;
            const bool ok = record_true(ctx, p, rp);
            SIM_REQUIRE(ok && routed(rp), "setup: parent " << p << " of " << i << " has no metric yet");
            Totals t = totals(ctx, i, p, rp);
            write_metric(ctx, i, t.primary, t.aux);
        }
        for (std::size_t i = 1; i < n; ++i) ctx.seed_view(ctx.nodes[i].id, record_of(ctx, ctx.nodes[i].id));
    }

    void control(AlgorithmContext& ctx) override {
        if (ctx.frame % period_ != 0) return;
        for (std::size_t i = 1; i < ctx.nodes.size(); ++i)
            if (ctx.nodes[i].alive) ctx.schedule_control(ctx.nodes[i].id, record_of(ctx, ctx.nodes[i].id));
    }

    void react(AlgorithmContext& ctx, NodeId p) override {
        snapshot(ctx);
        for (NodeId c : ctx.alive_children(p)) decide(ctx, c, false);
    }

    void rotate(AlgorithmContext& ctx) override {
        if (ctx.frame % period_ != 0) return;
        snapshot(ctx);
        for (std::size_t i = 1; i < ctx.nodes.size(); ++i)
            if (ctx.nodes[i].alive) decide(ctx, ctx.nodes[i].id, true);
    }

    void finish(AlgorithmContext& ctx) override {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        ctx.sink.set("pm_decisions", decisions_);
        ctx.sink.set("pm_no_candidate", no_candidate_);
        ctx.sink.set("pm_capped", capped_);
        ctx.sink.set("pm_metric_max", metric_max_ > 0.0 ? metric_max_ : nan);
    }

private:
    const variants::CostAttribution& attribution_;
    liu::Guards guards_;
    std::int64_t period_;
    const KnowledgeMode& mode_;
    energy::Radio radio_;
    double l_, R_max_;
    double x1_, x2_;

    double E_0_ = 0.0;
    std::vector<double> aux_;  // always 0 for this arm
    std::unique_ptr<NeighborView> view_;
    std::int64_t decisions_ = 0, no_candidate_ = 0, capped_ = 0;
    double metric_max_ = 0.0;
    std::int64_t snap_frame_ = -1;
    Stage snap_stage_ = Stage::Setup;

    struct Totals { double primary; double aux; };

    static double dist(const AlgorithmContext& ctx, NodeId a, NodeId b) {
        return geo::dist(ctx.topo.pos[static_cast<std::size_t>(a)], ctx.topo.pos[static_cast<std::size_t>(b)]);
    }

    void snapshot(AlgorithmContext& ctx) {
        if (snap_frame_ == ctx.frame && snap_stage_ == ctx.stage) return;
        snap_frame_ = ctx.frame;
        snap_stage_ = ctx.stage;
        view_->begin_stage([&](NodeId j) { return record_of(ctx, j); });
    }

    ViewRecord record_of(const AlgorithmContext& ctx, NodeId j) const {
        const Node& nd = ctx.nodes[static_cast<std::size_t>(j)];
        ViewRecord r;
        r.E = nd.E;
        r.P_hat = nd.P_hat;
        r.MTC = nd.MTC;
        r.PM2 = aux_[static_cast<std::size_t>(j)];
        r.route = nd.route;
        return r;
    }

    ViewRecord sink_record() const {
        ViewRecord r;
        r.MTC = 0.0;
        r.PM2 = 0.0;
        r.E = E_0_;
        r.P_hat = 0.0;
        r.route = true;
        return r;
    }

    bool record_for(NodeId i, NodeId j, ViewRecord& out) const {
        if (j == SINK) { out = sink_record(); return true; }
        return view_->lookup(i, j, out);
    }

    bool record_true(const AlgorithmContext& ctx, NodeId j, ViewRecord& out) const {
        if (j == SINK) { out = sink_record(); return true; }
        if (!ctx.nodes[static_cast<std::size_t>(j)].alive) return false;
        out = record_of(ctx, j);
        return true;
    }

    bool routed(const ViewRecord& r) const { return r.MTC < INF; }
    double routeless_metric() const { return INF; }

    double hop_E(const AlgorithmContext& ctx, NodeId i, const ViewRecord& rj) const {
        return attribution_.neighbour ? rj.E : ctx.nodes[static_cast<std::size_t>(i)].E;
    }
    double capped(double c) {
        if (!(c <= guards_.cap)) { c = guards_.cap; ++capped_; }
        SIM_INVARIANT("I12", std::isfinite(c), "path-metric cost " << c << " is not finite");
        return c;
    }

    Totals totals(const AlgorithmContext& ctx, NodeId i, NodeId j, const ViewRecord& rj) {
        const double e = energy::E_tx(radio_, l_, dist(ctx, i, j));
        const double frac = std::max(hop_E(ctx, i, rj), 0.0) / E_0_;
        const double c = frac > 0.0 ? std::pow(e, x1_) * std::pow(frac, -x2_) : INF;
        return {capped(c) + rj.MTC, 0.0};
    }

    bool better(double t, double best) const {
        const double tol = TIE_EPS * std::max(std::fabs(t), std::fabs(best));
        return t < best - tol;
    }

    void decide(AlgorithmContext& ctx, NodeId i, bool write_none) {
        const Node& nd = ctx.nodes[static_cast<std::size_t>(i)];
        NodeId best_id = NONE;
        Totals best{INF, 0.0};
        for (NodeId j : nd.nbr) {
            if (j == i) continue;
            if (!ctx.precedes(ctx.topo.tree.depth, j, i)) continue;
            ViewRecord rj;
            if (!record_for(i, j, rj)) continue;
            if (!routed(rj)) continue;
            const Totals t = totals(ctx, i, j, rj);
            if (best_id == NONE || better(t.primary, best.primary)) { best = t; best_id = j; }
        }
        if (best_id == NONE) {
            ++no_candidate_;
            if (write_none) ctx.set_parent(i, NONE);
            write_metric(ctx, i, routeless_metric(), 0.0);
            return;
        }
        ++decisions_;
        ctx.set_parent(i, best_id);
        write_metric(ctx, i, best.primary, best.aux);
    }

    void write_metric(AlgorithmContext& ctx, NodeId i, double primary, double aux) {
        SIM_REQUIRE(!std::isnan(primary), "frame " << ctx.frame << ": metric NaN for node " << i);
        ctx.set_mtc(i, primary);
        aux_[static_cast<std::size_t>(i)] = aux;
        if (std::isfinite(primary)) metric_max_ = std::max(metric_max_, primary);
    }
};

}

std::unique_ptr<Algorithm> make_fa_arm(const Config& cfg) { return std::make_unique<PathMetricArm>(cfg); }

}
