// radpr: proactive parent rotation by PRI score, with a switching margin Gamma.
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "arms/algorithm.hpp"
#include "core/assertions.hpp"
#include "core/config.hpp"
#include "io/rotation.hpp"
#include "model/energy.hpp"
#include "model/geometry.hpp"
#include "model/liu_cost.hpp"
#include "sim/context.hpp"
#include "sim/neighborview.hpp"
#include "sim/variants.hpp"

namespace sim {

namespace {

constexpr double TIE_EPS = 1e-12;   // relative
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double L_FLAG_BITS = 8.0;  // route bit receive size under costed control

double frac_term(double E, double E_0) { return std::clamp(E / E_0, 0.0, 1.0); }

// Single fraction, so P_hat = 0 gives exactly 1.
double trajectory_term(double E, double P_hat, double T_horizon) {
    if (E <= 0.0) return 0.0;
    return E / (E + P_hat * T_horizon);
}

struct Weights { double w1, w2, w3; };

// PRI = w1*frac + w2*R + w3*prox
double pri(const Weights& w, double frac, double R, double prox) { return w.w1 * frac + w.w2 * R + w.w3 * prox; }

// Delta_j = E_tx(l, d_j) + E_rx(l). The sink's Delta is 0.
double marginal_cost(const energy::Radio& radio, double l, double d_parent) {
    return energy::E_tx(radio, l, d_parent) + energy::E_rx(radio, l);
}

enum class Change { Rotate, Backup, Select, Request, Rejoin };

constexpr std::int64_t REQ_NONE = -1;
constexpr std::int64_t REQ_UNSENT = -2;

class RadprArm final : public Algorithm {
public:
    explicit RadprArm(const Config& cfg)
        : w_{cfg.w1, cfg.w2, cfg.w3},
          M_(cfg.M), Gamma_(cfg.Gamma), k_(cfg.k), T_orphan_(cfg.T_orphan),
          prox_(variants::proximity(cfg.radpr_proximity)),
          score_(variants::radpr_score(cfg.radpr_score)),
          attribution_(variants::dcfr_cost_attribution(cfg.dcfr_cost_attribution)),
          emap_(variants::escfr_energy_map(cfg.escfr_energy_map)),
          guards_{cfg.dcfr_sin_eps, cfg.dcfr_cost_cap},
          fa_x1_(cfg.fa_x1), fa_x2_(cfg.fa_x2),
          stranded_(variants::radpr_stranded(cfg.radpr_stranded)),
          backups_(variants::radpr_backups(cfg.radpr_backups)),
          mode_(variants::knowledge_mode(cfg.knowledge_mode)),
          radio_{cfg.E_elec, cfg.eps_fs},
          l_(static_cast<double>(cfg.l_data)),
          R_max_(cfg.R_max),
          log_gaps_(cfg.log_gaps) {
        SIM_REQUIRE(!(score_.path_cost && backups_.on),
                    "radpr_score = path_cost does not support radpr_backups = on (backups are chosen by PRI)");
    }

    void init(AlgorithmContext& ctx) override {
        const std::size_t n = ctx.nodes.size();
        E_0_ = ctx.topo.calib.E_0;
        T_horizon_ = ctx.res.T_horizon;
        delta_.assign(n, 0.0);
        known_route_.assign(n, 1);
        for (std::size_t i = 1; i < n; ++i) {
            const Node& nd = ctx.nodes[i];
            delta_[i] = marginal_cost(radio_, l_, dist(ctx, nd.id, nd.parent));
        }
        if (score_.path_cost) {
            std::vector<NodeId> order;
            for (std::size_t i = 1; i < n; ++i) order.push_back(ctx.nodes[i].id);
            std::stable_sort(order.begin(), order.end(), [&](NodeId a, NodeId b) {
                const auto da = ctx.nodes[static_cast<std::size_t>(a)].depth, db = ctx.nodes[static_cast<std::size_t>(b)].depth;
                if (da != db) return da < db;
                return a < b;
            });
            for (NodeId i : order) {
                const Node& nd = ctx.nodes[static_cast<std::size_t>(i)];
                SIM_REQUIRE(nd.parent != NONE, "setup: node " << i << " has no initial parent");
                ViewRecord rp;
                if (nd.parent == SINK) { rp = ViewRecord{}; rp.E = E_0_; rp.MTC = 0.0; rp.route = true; }
                else rp = record_of(ctx, nd.parent);
                SIM_REQUIRE(rp.MTC < INF, "setup: parent " << nd.parent << " of " << i << " has no finite MTC yet");
                ctx.set_mtc(i, path_total(ctx, i, nd.parent, rp));
            }
        }
        for (std::size_t i = 1; i < n; ++i) ctx.seed_view(ctx.nodes[i].id, record_of(ctx, ctx.nodes[i].id));
        view_ = std::make_unique<NeighborView>(mode_, ctx);
        if (log_gaps_) gaps_ = std::make_unique<GapsWriter>(ctx.out_prefix + "gaps.csv");
        rots_ = std::make_unique<RotationsWriter>(ctx.out_prefix + "rotations.csv");
        recs_ = std::make_unique<RecoveriesWriter>(ctx.out_prefix + "recoveries.csv");

        backup_.assign(n, NONE);
        n_backup_.assign(n, 0);
        request_.assign(n, REQ_NONE);
        sched_mark_.assign(n, 0);
        if (backups_.on) assign_backups(ctx);
    }

    void control(AlgorithmContext& ctx) override {
        const std::int64_t f = ctx.frame;
        std::fill(sched_mark_.begin(), sched_mark_.end(), 0);
        auto schedule = [&](NodeId u) {
            if (sched_mark_[static_cast<std::size_t>(u)]) return;
            sched_mark_[static_cast<std::size_t>(u)] = 1;
            ctx.schedule_control(u, record_of(ctx, u));
        };

        if (f % M_ == 0)
            for (std::size_t i = 1; i < ctx.nodes.size(); ++i)
                if (ctx.nodes[i].alive) schedule(ctx.nodes[i].id);

        for (NodeId v : responders_now_)
            if (ctx.nodes[static_cast<std::size_t>(v)].alive) schedule(v);
        responders_now_.clear();

        for (std::size_t i = 1; i < ctx.nodes.size(); ++i) {
            const Node& nd = ctx.nodes[i];
            if (!nd.alive || !nd.ctrl_due) continue;
            SIM_REQUIRE(request_[i] == REQ_UNSENT, "frame " << f << ": node " << nd.id << " has ctrl_due but no request raised in S5");
            schedule(nd.id);
            ++requests_sent_;
            request_[i] = f + T_orphan_ - 1;
            recs_->row(f, nd.id, "request_sent", NONE, NONE);
            for (NodeId v : nd.nbr)
                if (v != SINK && ctx.nodes[static_cast<std::size_t>(v)].alive) responders_next_.push_back(v);
        }
        std::sort(responders_next_.begin(), responders_next_.end());
        responders_next_.erase(std::unique(responders_next_.begin(), responders_next_.end()), responders_next_.end());
        std::swap(responders_now_, responders_next_);

        // Children of a sensor parent pay to overhear the route bit each frame.
        // The sink sends no data, so its children pay nothing.
        if (stranded_.signal)
            for (std::size_t i = 1; i < ctx.nodes.size(); ++i) {
                const Node& nd = ctx.nodes[i];
                if (nd.alive && nd.parent != NONE && nd.parent != SINK) ctx.charge_ctrl_rx(nd.id, L_FLAG_BITS);
            }
    }

    void react(AlgorithmContext& ctx, NodeId p) override {
        snapshot(ctx);
        const auto pi = static_cast<std::size_t>(p);
        end_backup_of(p);
        for (std::size_t i = 1; i < ctx.nodes.size(); ++i)
            if (backup_[i] == p) end_backup_of(static_cast<NodeId>(i));
        request_[pi] = REQ_NONE;

        for (NodeId c : ctx.alive_children(p)) {
            if (try_backup(ctx, c)) continue;
            if (select_parent(ctx, c, Change::Select)) continue;
            ctx.set_ctrl_due(c);
            request_[static_cast<std::size_t>(c)] = REQ_UNSENT;
            recs_->row(ctx.frame, c, "orphaned", p, NONE);
        }
    }

    // detach_signal ignores this call; it detaches from its own bit in S6.
    void orphan_rule(AlgorithmContext& ctx, NodeId i) override {
        if (!stranded_.detach) return;
        detach(ctx, i);
    }

    void rotate(AlgorithmContext& ctx) override {
        snapshot(ctx);
        if (stranded_.signal) learn_route_bits(ctx);
        rotate_decisions(ctx);
        if (stranded_.signal) settle_route_bits(ctx);
    }

    void rotate_decisions(AlgorithmContext& ctx) {
        const std::int64_t f = ctx.frame;
        for (std::size_t i = 1; i < ctx.nodes.size(); ++i) {
            const Node& nd = ctx.nodes[i];
            if (!nd.alive) continue;
            if (nd.parent != NONE) {
                if (f % M_ == 0) evaluate(ctx, nd.id);
                continue;
            }
            const std::int64_t req = request_[i];
            if (req == REQ_UNSENT || (req >= 0 && f < req)) continue;   // window still open
            if (req >= 0) {
                request_[i] = REQ_NONE;
                if (!select_parent(ctx, nd.id, Change::Request)) recs_->row(f, nd.id, "request_failed", NONE, NONE);
                continue;
            }
            select_parent(ctx, nd.id, Change::Rejoin);
        }
    }

    void finish(AlgorithmContext& ctx) override {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        std::sort(gaps_seen_.begin(), gaps_seen_.end());
        ctx.sink.set("rotations_total", rotations_);
        ctx.sink.set("gap_evaluations", static_cast<std::int64_t>(gaps_seen_.size()));
        ctx.sink.set("gap_p05", gaps_seen_.empty() ? nan : percentile(0.05));
        ctx.sink.set("gap_p50", gaps_seen_.empty() ? nan : percentile(0.50));
        ctx.sink.set("gap_p95", gaps_seen_.empty() ? nan : percentile(0.95));
        ctx.sink.set("gap_above_gamma", gap_above_gamma_);
        ctx.sink.set("rotations_routeless", rotations_routeless_);
        ctx.sink.set("recoveries_backup", recoveries_backup_);
        ctx.sink.set("recoveries_select", recoveries_select_);
        ctx.sink.set("recoveries_request", recoveries_request_);
        ctx.sink.set("rejoins", rejoins_);
        ctx.sink.set("requests_sent", requests_sent_);
        ctx.sink.set("backups_assigned", backups_assigned_);
        ctx.sink.set("radpr_detached", detached_);
        if (gaps_) gaps_->close();
        rots_->close();
        recs_->close();
    }

private:
    Weights w_;
    std::int64_t M_;
    double Gamma_;
    std::int64_t k_;
    std::int64_t T_orphan_;
    variants::ProximityFn prox_;
    const variants::ScoreRule& score_;
    const variants::CostAttribution& attribution_;
    variants::EnergyMapFn emap_;
    liu::Guards guards_;
    double fa_x1_, fa_x2_;
    const variants::Stranded& stranded_;
    const variants::Backups& backups_;
    const KnowledgeMode& mode_;
    energy::Radio radio_;
    double l_;
    double R_max_;
    bool log_gaps_;

    double E_0_ = 0.0, T_horizon_ = 0.0;
    std::vector<double> delta_;
    std::unique_ptr<NeighborView> view_;
    std::unique_ptr<GapsWriter> gaps_;
    std::unique_ptr<RotationsWriter> rots_;
    std::unique_ptr<RecoveriesWriter> recs_;

    std::vector<NodeId> backup_;
    std::vector<std::int32_t> n_backup_;

    std::vector<std::int64_t> request_;
    std::vector<NodeId> responders_now_, responders_next_;
    std::vector<char> sched_mark_;

    std::int64_t rotations_ = 0, gap_above_gamma_ = 0, rotations_routeless_ = 0;
    std::int64_t recoveries_backup_ = 0, recoveries_select_ = 0, recoveries_request_ = 0, rejoins_ = 0;
    std::int64_t requests_sent_ = 0, backups_assigned_ = 0, detached_ = 0;
    std::vector<double> gaps_seen_;
    std::int64_t snap_frame_ = -1;
    Stage snap_stage_ = Stage::Setup;

    // Arm-local route bits: current, and as of the end of the previous frame.
    std::vector<char> known_route_, prev_route_;

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
        r.Delta = delta_[static_cast<std::size_t>(j)];
        r.MTC = nd.MTC;
        r.route = stranded_.signal ? known_route_[static_cast<std::size_t>(j)] != 0 : nd.route;
        return r;
    }

    // False with no parent or a dead one, or under a sensor parent whose bit was
    // false last frame. The sink is always routed.
    static bool route_bit(const AlgorithmContext& ctx, NodeId i, const std::vector<char>& prev) {
        const NodeId p = ctx.nodes[static_cast<std::size_t>(i)].parent;
        if (p == NONE) return false;
        if (p == SINK) return true;
        if (!ctx.nodes[static_cast<std::size_t>(p)].alive) return false;
        return prev[static_cast<std::size_t>(p)] != 0;
    }

    void learn_route_bits(AlgorithmContext& ctx) {
        prev_route_ = known_route_;
        for (std::size_t i = 1; i < ctx.nodes.size(); ++i) {
            const Node& nd = ctx.nodes[i];
            if (!nd.alive) { known_route_[i] = 0; continue; }
            const bool routed = route_bit(ctx, nd.id, prev_route_);
            known_route_[i] = routed ? 1 : 0;
            if (!routed && nd.parent != NONE) detach(ctx, nd.id);
        }
    }

    void settle_route_bits(AlgorithmContext& ctx) {
        for (std::size_t i = 1; i < ctx.nodes.size(); ++i) {
            const Node& nd = ctx.nodes[i];
            known_route_[i] = nd.alive && route_bit(ctx, nd.id, prev_route_) ? 1 : 0;
        }
    }

    void detach(AlgorithmContext& ctx, NodeId i) {
        ctx.set_parent(i, NONE);
        if (score_.path_cost) ctx.set_mtc(i, INF);
        ++detached_;
        recs_->row(ctx.frame, i, "detached", ctx.nodes[static_cast<std::size_t>(i)].last_parent, NONE);
    }

    double path_total(const AlgorithmContext& ctx, NodeId i, NodeId j, const ViewRecord& rj) const {
        const double e = energy::E_tx(radio_, l_, dist(ctx, i, j));
        const double E = attribution_.neighbour ? rj.E : ctx.nodes[static_cast<std::size_t>(i)].E;
        double c;
        if (score_.fa) {
            // fa_cost: e^x1 * (E/E_0)^(-x2), capped
            const double frac = std::max(E, 0.0) / E_0_;
            c = frac > 0.0 ? std::pow(e, fa_x1_) * std::pow(frac, -fa_x2_) : INF;
            if (!(c <= guards_.cap)) c = guards_.cap;
        } else {
            c = liu::priced(e, emap_(E / E_0_), guards_).c;
        }
        SIM_INVARIANT("I12", std::isfinite(c), "frame " << ctx.frame << ": cost " << c << " for " << i << " -> " << j);
        return c + rj.MTC;
    }

    double score(const AlgorithmContext& ctx, NodeId i, NodeId j, const ViewRecord& rj, double m) const {
        if (score_.path_cost) return -path_total(ctx, i, j, rj);
        const double P = rj.P_hat + m * rj.Delta;
        return pri(w_, frac_term(rj.E, E_0_), trajectory_term(rj.E, P, T_horizon_), prox_(dist(ctx, i, j), R_max_));
    }

    bool scorable(const ViewRecord& rj) const { return rj.route && (!score_.path_cost || rj.MTC < INF); }

    bool record_for(NodeId i, NodeId j, ViewRecord& out) const {
        if (j == SINK) {
            out = ViewRecord{};
            out.E = E_0_;
            out.P_hat = 0.0;
            out.Delta = 0.0;
            out.MTC = 0.0;
            out.route = true;
            return true;
        }
        return view_->lookup(i, j, out);
    }

    NodeId best_candidate(const AlgorithmContext& ctx, NodeId i, NodeId exclude, double m, double& best) const {
        const Node& nd = ctx.nodes[static_cast<std::size_t>(i)];
        NodeId best_id = NONE;
        best = -std::numeric_limits<double>::infinity();
        for (NodeId j : nd.nbr) {
            if (j == exclude || j == i) continue;
            if (!ctx.precedes(ctx.topo.tree.depth, j, i)) continue;
            ViewRecord rj;
            if (!record_for(i, j, rj)) continue;
            if (!scorable(rj)) continue;
            const double s = score(ctx, i, j, rj, m);
            if (best_id == NONE || s > best + TIE_EPS * std::max(std::fabs(s), std::fabs(best))) {
                best = s;
                best_id = j;
            }
        }
        return best_id;
    }

    // m_i = 1 + B_i / l, from the previous frame's receptions.
    static double load_of(const AlgorithmContext& ctx, NodeId i) {
        return 1.0 + static_cast<double>(ctx.nodes[static_cast<std::size_t>(i)].recv);
    }

    void assign_backups(AlgorithmContext& ctx) {
        snapshot(ctx);
        for (std::size_t i = 1; i < ctx.nodes.size(); ++i) {
            const Node& nd = ctx.nodes[i];
            struct Scored { double s; NodeId j; };
            std::vector<Scored> cands;
            for (NodeId j : nd.nbr) {
                if (j == nd.parent || j == nd.id) continue;
                if (!ctx.precedes(ctx.topo.tree.depth, j, nd.id)) continue;
                ViewRecord rj;
                if (!record_for(nd.id, j, rj)) continue;
                if (!rj.route) continue;
                cands.push_back({pri(w_, frac_term(rj.E, E_0_), trajectory_term(rj.E, rj.P_hat, T_horizon_),
                                     prox_(dist(ctx, nd.id, j), R_max_)), j});
            }
            std::stable_sort(cands.begin(), cands.end(), [](const Scored& a, const Scored& b) {
                const double tol = TIE_EPS * std::max(std::fabs(a.s), std::fabs(b.s));
                if (std::fabs(a.s - b.s) <= tol) return a.j < b.j;
                return a.s > b.s;
            });
            for (const Scored& c : cands) {
                if (n_backup_[static_cast<std::size_t>(c.j)] >= k_) continue;
                backup_[i] = c.j;
                ++n_backup_[static_cast<std::size_t>(c.j)];
                ++backups_assigned_;
                break;
            }
            recs_->row(0, nd.id, "assign", NONE, backup_[i]);
        }
    }

    void end_backup_of(NodeId i) {
        const auto ii = static_cast<std::size_t>(i);
        if (backup_[ii] == NONE) return;
        auto& n = n_backup_[static_cast<std::size_t>(backup_[ii])];
        if (n > 0) --n;
        backup_[ii] = NONE;
    }

    bool try_backup(AlgorithmContext& ctx, NodeId c) {
        const NodeId b = backup_[static_cast<std::size_t>(c)];
        if (b == NONE) return false;
        ViewRecord rb;
        if (!record_for(c, b, rb) || !rb.route) return false;
        SIM_REQUIRE(ctx.precedes(ctx.topo.tree.depth, b, c), "frame " << ctx.frame << ": backup " << b << " of " << c << " does not precede it");
        change_parent(ctx, c, b, Change::Backup);
        return true;
    }

    bool select_parent(AlgorithmContext& ctx, NodeId i, Change why) {
        double best;
        const NodeId j = best_candidate(ctx, i, NONE, load_of(ctx, i), best);
        if (j == NONE) {
            if (score_.path_cost) ctx.set_mtc(i, INF);
            return false;
        }
        change_parent(ctx, i, j, why);
        return true;
    }

    void change_parent(AlgorithmContext& ctx, NodeId i, NodeId p, Change why) {
        const auto ii = static_cast<std::size_t>(i);
        const NodeId old = ctx.nodes[ii].parent;
        ctx.set_parent(i, p);
        if (score_.path_cost) {
            ViewRecord rp;
            const bool ok = record_for(i, p, rp);
            SIM_REQUIRE(ok, "frame " << ctx.frame << ": node " << i << " chose " << p << " without a record for it");
            ctx.set_mtc(i, path_total(ctx, i, p, rp));
        }
        if (old != NONE && old == backup_[ii]) end_backup_of(i);
        delta_[ii] = marginal_cost(radio_, l_, dist(ctx, i, p));
        switch (why) {
            case Change::Rotate:  ++rotations_;          rots_->row(ctx.frame, i, old, p); break;
            case Change::Backup:  ++recoveries_backup_;  recs_->row(ctx.frame, i, "backup", old, p); break;
            case Change::Select:  ++recoveries_select_;  recs_->row(ctx.frame, i, "select", old, p); break;
            case Change::Request: ++recoveries_request_; recs_->row(ctx.frame, i, "request_ok", old, p); break;
            case Change::Rejoin:  ++rejoins_;            recs_->row(ctx.frame, i, "rejoin", old, p); break;
        }
    }

    void evaluate(AlgorithmContext& ctx, NodeId i) {
        const Node& nd = ctx.nodes[static_cast<std::size_t>(i)];
        const NodeId inc = nd.parent;

        // Incumbent unprimed: its measured load already includes i.
        ViewRecord rin;
        SIM_REQUIRE(record_for(i, inc, rin), "frame " << ctx.frame << ": node " << i << " holds no record for its parent " << inc);
        const double pri_inc = score(ctx, i, inc, rin, 0.0);

        // Candidates primed with i's load.
        double best;
        const NodeId best_id = best_candidate(ctx, i, inc, load_of(ctx, i), best);
        if (best_id == NONE) {
            // Re-advertise the path cost even with no alternative; otherwise the
            // children of a depth-1 node never see its cost rise.
            if (score_.path_cost) ctx.set_mtc(i, scorable(rin) ? -pri_inc : INF);
            return;
        }

        // Switch iff best > incumbent + Gamma. The margin is waived when the
        // incumbent has no route.
        const double gap = best - pri_inc;
        const bool inc_routed = scorable(rin);
        const bool switched = best > pri_inc + (inc_routed ? Gamma_ : 0.0);
        gaps_seen_.push_back(gap);
        if (gaps_) gaps_->row(ctx.frame, i, inc, best_id, pri_inc, best, gap, switched, inc_routed);
        if (!switched) {
            if (score_.path_cost) ctx.set_mtc(i, inc_routed ? -pri_inc : INF);
            return;
        }
        if (gap > Gamma_) ++gap_above_gamma_;
        else ++rotations_routeless_;
        change_parent(ctx, i, best_id, Change::Rotate);
    }

    // Nearest-rank percentile.
    double percentile(double p) const {
        const auto n = gaps_seen_.size();
        std::size_t k = static_cast<std::size_t>(std::ceil(p * static_cast<double>(n)));
        if (k > 0) --k;
        if (k >= n) k = n - 1;
        return gaps_seen_[k];
    }
};

}

std::unique_ptr<Algorithm> make_radpr_arm(const Config& cfg) { return std::make_unique<RadprArm>(cfg); }

}
