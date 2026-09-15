// dcfr and escfr: Liu et al. 2012 cost-function routing.
// escfr is the same class with the rate term left out.
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "arms/algorithm.hpp"
#include "core/assertions.hpp"
#include "core/config.hpp"
#include "io/dcfr_log.hpp"
#include "model/energy.hpp"
#include "model/geometry.hpp"
#include "model/liu_cost.hpp"
#include "sim/context.hpp"
#include "sim/neighborview.hpp"
#include "sim/variants.hpp"

namespace sim {

namespace {

constexpr double TIE_EPS = 1e-12;   // relative
constexpr double PI = liu::PI;
constexpr double INF = std::numeric_limits<double>::infinity();

// Weights a hop: residual energy, rate over the latest window, stored maximum rate.
struct HopState {
    double E;
    double es;
    double mes;
};

class DcfrArm final : public Algorithm {
public:
    DcfrArm(const Config& cfg, bool rate_term)
        : rate_term_(rate_term),
          attribution_(variants::dcfr_cost_attribution(cfg.dcfr_cost_attribution)),
          emap_(variants::escfr_energy_map(cfg.escfr_energy_map)),
          normaliser_(variants::dcfr_rate_normaliser(cfg.dcfr_rate_normaliser)),
          period_(variants::dcfr_period(cfg.dcfr_period)(cfg.M)),
          W_(variants::dcfr_rate_window(cfg.dcfr_rate_window)(period_, cfg.M)),
          accum_(variants::dcfr_path_accumulation(cfg.dcfr_path_accumulation)),
          mode_(variants::knowledge_mode(cfg.knowledge_mode)),
          radio_{cfg.E_elec, cfg.eps_fs},
          l_(static_cast<double>(cfg.l_data)),
          guards_{cfg.dcfr_sin_eps, cfg.dcfr_cost_cap},
          log_frames_(cfg.dcfr_log_frames) {
        SIM_REQUIRE(period_ >= 1, "dcfr_period resolved to " << period_ << " frames; must be >= 1");
        SIM_REQUIRE(W_ >= 1, "dcfr_rate_window resolved to " << W_ << " frames; must be >= 1");
    }

    void init(AlgorithmContext& ctx) override {
        const std::size_t n = ctx.nodes.size();
        E_0_ = ctx.topo.calib.E_0;
        P_max_ = ctx.topo.calib.P_max;
        es_.assign(n, 0.0);
        mes_.assign(n, 0.0);
        for (std::size_t i = 1; i < n; ++i) {
            es_[i] = ctx.topo.calib.P[i];
            mes_[i] = ctx.topo.calib.P[i];
            network_max_ = std::max(network_max_, mes_[i]);
        }
        hist_.assign((static_cast<std::size_t>(W_) + 1) * n, E_0_);

        view_ = std::make_unique<NeighborView>(mode_, ctx);
        if (log_frames_ > 0) log_ = std::make_unique<DcfrDecisionsWriter>(ctx.out_prefix + "dcfr_decisions.csv");

        // MTC is filled shallow to deep, so a parent's value is final before a child adds to it.
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
            SIM_REQUIRE(ok && rp.MTC < INF, "setup: parent " << p << " of " << i << " has no finite MTC yet");
            const double total = hop_total(ctx, i, p, rp, rate_normaliser(ctx, i), false);
            write_mtc(ctx, i, total);
        }
        for (std::size_t i = 1; i < n; ++i) ctx.seed_view(ctx.nodes[i].id, record_of(ctx, ctx.nodes[i].id));
    }

    void control(AlgorithmContext& ctx) override {
        const std::int64_t f = ctx.frame;
        const std::size_t n = ctx.nodes.size();
        for (std::size_t i = 1; i < n; ++i)
            if (ctx.nodes[i].alive) hist_[slot(f - 1) * n + i] = ctx.nodes[i].E;
        if (f % period_ != 0) return;
        for (std::size_t i = 1; i < n; ++i) {
            const Node& nd = ctx.nodes[i];
            if (!nd.alive) continue;
            const double E_old = hist_[slot(f - 1 - W_) * n + i];
            const double es = (E_old - nd.E) / static_cast<double>(W_);
            const double mes = std::max(mes_[i], es);
            SIM_INVARIANT("I13", mes >= mes_[i], "frame " << f << ": MES of node " << nd.id << " would fall from "
                                                          << mes_[i] << " to " << mes);
            es_[i] = es;
            mes_[i] = mes;
            network_max_ = std::max(network_max_, mes);
            if (W_ == 1 && period_ == 1)
                SIM_REQUIRE(std::fabs(mes - nd.MES) <= 1e-9 * std::max(mes, nd.MES),
                            "frame " << f << ": arm MES " << mes << " != loop MES " << nd.MES << " for node " << nd.id);
            ctx.schedule_control(nd.id, record_of(ctx, nd.id));
        }
    }

    void react(AlgorithmContext& ctx, NodeId p) override {
        snapshot(ctx);
        for (NodeId c : ctx.alive_children(p)) decide(ctx, c, "S5", false);
    }

    void rotate(AlgorithmContext& ctx) override {
        if (ctx.frame % period_ != 0) return;
        snapshot(ctx);
        for (std::size_t i = 1; i < ctx.nodes.size(); ++i)
            if (ctx.nodes[i].alive) decide(ctx, ctx.nodes[i].id, "S6", true);
    }

    void finish(AlgorithmContext& ctx) override {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        ctx.sink.set("dcfr_cost_max", cost_max_);
        ctx.sink.set("dcfr_nonfinite_costs", nonfinite_);
        ctx.sink.set("dcfr_cost_capped", capped_);
        ctx.sink.set("dcfr_rule1_bindings", rule1_bindings_);
        ctx.sink.set("dcfr_rule1_active", rule1_active_);
        ctx.sink.set("dcfr_decisions", decisions_);
        ctx.sink.set("dcfr_no_candidate", no_candidate_);
        ctx.sink.set("dcfr_mtc_max", mtc_max_ > 0.0 ? mtc_max_ : nan);
        if (log_) log_->close();
    }

private:
    bool rate_term_;
    const variants::CostAttribution& attribution_;
    variants::EnergyMapFn emap_;
    const variants::RateNormaliser& normaliser_;
    std::int64_t period_;
    std::int64_t W_;
    const variants::PathAccumulation& accum_;
    const KnowledgeMode& mode_;
    energy::Radio radio_;
    double l_;
    liu::Guards guards_;
    std::int64_t log_frames_;

    double E_0_ = 0.0, P_max_ = 0.0;
    std::vector<double> es_;      // rate over the latest window
    std::vector<double> mes_;     // running maximum of es_ (Liu Rule 1)
    std::vector<double> hist_;    // E at the end of frame t, index slot(t) * n + i
    double network_max_ = 0.0;
    std::unique_ptr<NeighborView> view_;
    std::unique_ptr<DcfrDecisionsWriter> log_;

    double cost_max_ = 0.0, mtc_max_ = 0.0;
    std::int64_t nonfinite_ = 0, capped_ = 0;
    std::int64_t rule1_bindings_ = 0, rule1_active_ = 0, decisions_ = 0, no_candidate_ = 0;
    std::int64_t snap_frame_ = -1;
    Stage snap_stage_ = Stage::Setup;

    std::size_t slot(std::int64_t t) const {
        const std::int64_t m = W_ + 1;
        return static_cast<std::size_t>(((t % m) + m) % m);
    }

    static double dist(const AlgorithmContext& ctx, NodeId a, NodeId b) {
        return geo::dist(ctx.topo.pos[static_cast<std::size_t>(a)], ctx.topo.pos[static_cast<std::size_t>(b)]);
    }

    void snapshot(AlgorithmContext& ctx) {
        if (snap_frame_ == ctx.frame && snap_stage_ == ctx.stage) return;
        snap_frame_ = ctx.frame;
        snap_stage_ = ctx.stage;
        view_->begin_stage([&](NodeId j) { return record_of(ctx, j); });
    }

    // Advertises the stored MTC, not the loop's route flag, so a broken chain
    // propagates one hop per period.
    ViewRecord record_of(const AlgorithmContext& ctx, NodeId j) const {
        const Node& nd = ctx.nodes[static_cast<std::size_t>(j)];
        ViewRecord r;
        r.MTC = nd.MTC;
        r.E = nd.E;
        r.CES = es_[static_cast<std::size_t>(j)];
        r.MES = mes_[static_cast<std::size_t>(j)];
        r.route = nd.route;
        return r;
    }

    static ViewRecord sink_record(double E_0) {
        ViewRecord r;
        r.MTC = 0.0;
        r.E = E_0;
        r.CES = 0.0;
        r.MES = 0.0;
        r.route = true;
        return r;
    }

    bool record_for(NodeId i, NodeId j, ViewRecord& out) const {
        if (j == SINK) { out = sink_record(E_0_); return true; }
        return view_->lookup(i, j, out);
    }

    bool record_true(const AlgorithmContext& ctx, NodeId j, ViewRecord& out) const {
        if (j == SINK) { out = sink_record(E_0_); return true; }
        if (!ctx.nodes[static_cast<std::size_t>(j)].alive) return false;
        out = record_of(ctx, j);
        return true;
    }

    HopState own_state(const AlgorithmContext& ctx, NodeId i) const {
        const auto ii = static_cast<std::size_t>(i);
        return {ctx.nodes[ii].E, es_[ii], mes_[ii]};
    }

    // The sin clamp alone still overflows exp(); the cap keeps a dying node finite.
    double priced(double e, double theta) {
        const liu::Priced p = liu::priced(e, theta, guards_);
        const double c = p.c;
        if (p.capped) ++capped_;
        if (!std::isfinite(c)) ++nonfinite_;
        SIM_INVARIANT("I12", std::isfinite(c), "cost " << c << " from e=" << e << " theta=" << theta);
        cost_max_ = std::max(cost_max_, c);
        return c;
    }

    double energy_cost(double e, double E) { return priced(e, emap_(E / E_0_)); }

    // r(MES) = pi/2 + (pi/2) * min(1, MES / R_rate)
    double rate_cost(double e, double rate, double R_rate) {
        SIM_REQUIRE(R_rate > 0.0, "rate normaliser R_rate = " << R_rate << " is not positive");
        return priced(e, PI / 2.0 + (PI / 2.0) * std::min(1.0, rate / R_rate));
    }

    double rate_normaliser(const AlgorithmContext& ctx, NodeId i) const {
        switch (normaliser_.scope) {
            case variants::RateScope::Calibration: return P_max_;
            case variants::RateScope::Network: return network_max_;
            case variants::RateScope::Neighbourhood: {
                double m = mes_[static_cast<std::size_t>(i)];
                for (NodeId j : ctx.nodes[static_cast<std::size_t>(i)].nbr) {
                    if (j == SINK) continue;
                    ViewRecord rj;
                    if (record_for(i, j, rj)) m = std::max(m, rj.MES);
                }
                return m;
            }
        }
        return P_max_;
    }

    // `instant` uses the instantaneous rate in place of the stored MES (Rule 1 counterfactual).
    double hop_total(const AlgorithmContext& ctx, NodeId i, NodeId j, const ViewRecord& rj, double R_rate,
                     bool instant) {
        const double e = energy::E_tx(radio_, l_, dist(ctx, i, j));
        const HopState st = attribution_.neighbour ? HopState{rj.E, rj.CES, rj.MES} : own_state(ctx, i);
        double total = energy_cost(e, st.E);
        if (rate_term_) total += rate_cost(e, instant ? st.es : st.mes, R_rate);
        if (accum_.on) total += rj.MTC;
        return total;
    }

    bool rule1_relevant(const AlgorithmContext& ctx, NodeId i, const ViewRecord& rj) const {
        if (!rate_term_) return false;
        const HopState st = attribution_.neighbour ? HopState{rj.E, rj.CES, rj.MES} : own_state(ctx, i);
        return st.mes > st.es;
    }

    // Ties keep the earlier candidate, so the lowest id wins. NONE if there is no candidate.
    NodeId argmin(AlgorithmContext& ctx, NodeId i, double R_rate, bool instant, double& best, std::int64_t& n_cand,
                  bool& rule1) {
        const Node& nd = ctx.nodes[static_cast<std::size_t>(i)];
        NodeId best_id = NONE;
        best = INF;
        n_cand = 0;
        rule1 = false;
        for (NodeId j : nd.nbr) {
            if (j == i) continue;
            if (!ctx.precedes(ctx.topo.tree.depth, j, i)) continue;
            ViewRecord rj;
            if (!record_for(i, j, rj)) continue;
            if (!(rj.MTC < INF)) continue;
            ++n_cand;
            if (rule1_relevant(ctx, i, rj)) rule1 = true;
            const double t = hop_total(ctx, i, j, rj, R_rate, instant);
            SIM_INVARIANT("I12", std::isfinite(t), "frame " << ctx.frame << ": total " << t << " for " << i << " -> " << j);
            if (best_id == NONE || t < best - TIE_EPS * std::max(std::fabs(t), std::fabs(best))) {
                best = t;
                best_id = j;
            }
        }
        return best_id;
    }

    void decide(AlgorithmContext& ctx, NodeId i, const char* stage, bool write_none) {
        const NodeId old = ctx.nodes[static_cast<std::size_t>(i)].parent;
        const double R_rate = rate_term_ ? rate_normaliser(ctx, i) : 0.0;
        double best;
        std::int64_t n_cand;
        bool rule1;
        const NodeId j = argmin(ctx, i, R_rate, false, best, n_cand, rule1);
        bool binding = false;
        if (j == NONE) {
            ++no_candidate_;
            if (write_none) ctx.set_parent(i, NONE);
            ctx.set_mtc(i, INF);
        } else {
            ++decisions_;
            if (rule1) {
                ++rule1_active_;
                double alt_best;
                std::int64_t alt_n;
                bool alt_rule1;
                const NodeId alt = argmin(ctx, i, R_rate, true, alt_best, alt_n, alt_rule1);
                binding = alt != j;
                if (binding) ++rule1_bindings_;
            }
            ctx.set_parent(i, j);
            write_mtc(ctx, i, best);
        }
        if (log_ && ctx.frame <= log_frames_) log_->row(ctx.frame, stage, i, old, j, best, n_cand, rule1, binding);
    }

    void write_mtc(AlgorithmContext& ctx, NodeId i, double mtc) {
        SIM_INVARIANT("I12", std::isfinite(mtc), "frame " << ctx.frame << ": MTC " << mtc << " for node " << i);
        ctx.set_mtc(i, mtc);
        mtc_max_ = std::max(mtc_max_, mtc);
    }
};

}

std::unique_ptr<Algorithm> make_dcfr_arm(const Config& cfg) { return std::make_unique<DcfrArm>(cfg, true); }
std::unique_ptr<Algorithm> make_escfr_arm(const Config& cfg) { return std::make_unique<DcfrArm>(cfg, false); }

}
