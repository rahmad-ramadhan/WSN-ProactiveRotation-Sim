// nemcr: Urmonov & Kim 2018. Reacts to parent death only and never broadcasts
// after setup. Stage A: a child that reaches the grandparent takes over.
// Stage B: the best (child, neighbour backup parent) pair outside the dead subtree.
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "arms/algorithm.hpp"
#include "core/assertions.hpp"
#include "core/config.hpp"
#include "io/recovery_log.hpp"
#include "model/geometry.hpp"
#include "sim/context.hpp"
#include "sim/variants.hpp"

namespace sim {

namespace {

constexpr double TIE_EPS = 1e-12;   // relative

struct Plan {
    enum class Stage { None, A, B };
    Stage stage = Stage::None;
    NodeId c = NONE;          // takeover child
    NodeId target = NONE;     // its new parent
    bool a_empty = true;      // no child in range of the grandparent
};

bool tie(double a, double b) { return std::fabs(a - b) <= TIE_EPS * std::max(std::fabs(a), std::fabs(b)); }

class NemcrArm final : public Algorithm {
public:
    explicit NemcrArm(const Config& cfg)
        : refresh_(variants::nemcr_backup_refresh(cfg.nemcr_backup_refresh)),
          inherit_(variants::nemcr_slot_inherit(cfg.nemcr_slot_inherit)),
          eps_fs_(cfg.eps_fs),
          l_(static_cast<double>(cfg.l_data)),
          R_max_(cfg.R_max) {}

    InvariantPolicy invariant_policy() const override { return {true, true}; }

    void init(AlgorithmContext& ctx) override {
        events_ = std::make_unique<NemcrRecoveryWriter>(ctx.out_prefix + "nemcr_recovery.csv");
        links_ = std::make_unique<NemcrLinksWriter>(ctx.out_prefix + "nemcr_links.csv");
        if (refresh_.mode == variants::RefreshMode::OnFailure) return;
        plans_.assign(ctx.nodes.size(), Plan{});
        planned_.assign(ctx.nodes.size(), 0);
        for (std::size_t p = 1; p < ctx.nodes.size(); ++p) {
            const std::vector<NodeId> S = ctx.alive_children(static_cast<NodeId>(p));
            if (S.empty()) continue;
            plans_[p] = evaluate(ctx, static_cast<NodeId>(p), S, ctx.nodes[p].parent);
            planned_[p] = 1;
        }
    }

    void react(AlgorithmContext& ctx, NodeId p) override {
        const auto pi = static_cast<std::size_t>(p);
        const NodeId g = ctx.nodes[pi].parent;
        const std::vector<NodeId> S = ctx.alive_children(p);
        if (S.empty()) {
            events_->row(ctx.frame, p, g, 0, "-", NONE, NONE, 0, 0, false, 0);
            return;
        }
        children_total_ += static_cast<std::int64_t>(S.size());

        Plan plan;
        const char* stage = nullptr;
        if (refresh_.mode == variants::RefreshMode::OnFailure) {
            plan = evaluate(ctx, p, S, g);
        } else if (!planned_[pi]) {
            // p had no children at setup, so no plan exists.
            stage = "unplanned";
            ++stale_;
        } else if (refresh_.mode == variants::RefreshMode::Frozen) {
            plan = plans_[pi];
            if (plan.stage != Plan::Stage::None && !applicable(ctx, p, plan)) {
                stage = "stale";
                ++stale_;
                plan.stage = Plan::Stage::None;
            }
        } else {
            const Plan& fixed = plans_[pi];
            if (fixed.stage == Plan::Stage::None) {
                plan = fixed;
            } else {
                const Node& c = ctx.nodes[static_cast<std::size_t>(fixed.c)];
                if (!c.alive || c.parent != p) {
                    stage = "stale";
                    ++stale_;
                } else {
                    plan = evaluate(ctx, p, S, g, fixed.c);
                }
            }
        }
        if (stage == nullptr) {
            switch (plan.stage) {
                case Plan::Stage::A: stage = "A"; ++stage_a_; break;
                case Plan::Stage::B: stage = "B"; ++stage_b_; break;
                case Plan::Stage::None: stage = "none"; ++unrecoverable_; break;
            }
        }

        std::int64_t attached = 0, orphaned = 0, new_violations = 0;
        bool inherited = false;
        apply(ctx, p, S, plan, attached, orphaned, inherited, new_violations);
        attached_total_ += attached;
        events_->row(ctx.frame, p, g, static_cast<std::int64_t>(S.size()), stage,
                     plan.stage == Plan::Stage::None ? NONE : plan.c,
                     plan.stage == Plan::Stage::None ? NONE : plan.target,
                     attached, orphaned, inherited, new_violations);
    }

    void finish(AlgorithmContext& ctx) override {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        ctx.sink.set("nemcr_out_of_reach_frac",
                     evaluations_ > 0 ? static_cast<double>(out_of_reach_) / static_cast<double>(evaluations_) : nan);
        ctx.sink.set("nemcr_evaluations", evaluations_);
        ctx.sink.set("nemcr_stage_a", stage_a_);
        ctx.sink.set("nemcr_stage_b", stage_b_);
        ctx.sink.set("nemcr_unrecoverable", unrecoverable_);
        ctx.sink.set("nemcr_stale_plans", stale_);
        events_->close();
        links_->close();
    }

private:
    const variants::BackupRefresh& refresh_;
    const variants::SlotInherit& inherit_;
    double eps_fs_;
    double l_;
    double R_max_;

    std::unique_ptr<NemcrRecoveryWriter> events_;
    std::unique_ptr<NemcrLinksWriter> links_;
    std::vector<Plan> plans_;
    std::vector<char> planned_;

    std::int64_t evaluations_ = 0, out_of_reach_ = 0;
    std::int64_t stage_a_ = 0, stage_b_ = 0, unrecoverable_ = 0, stale_ = 0;
    std::int64_t children_total_ = 0, attached_total_ = 0;

    static double dist(const AlgorithmContext& ctx, NodeId a, NodeId b) {
        return geo::dist(ctx.topo.pos[static_cast<std::size_t>(a)], ctx.topo.pos[static_cast<std::size_t>(b)]);
    }
    bool in_range(const AlgorithmContext& ctx, NodeId a, NodeId b) const {
        return geo::within(ctx.topo.pos[static_cast<std::size_t>(a)], ctx.topo.pos[static_cast<std::size_t>(b)], R_max_);
    }

    static bool routed_now(const AlgorithmContext& ctx, NodeId x) {
        if (x == SINK) return true;
        return ctx.nodes[static_cast<std::size_t>(x)].alive && ctx.live_chain(x) >= 0;
    }

    // Includes dead nodes and p itself.
    static std::vector<char> descendants_of(const AlgorithmContext& ctx, NodeId p) {
        const auto n = ctx.nodes.size();
        std::vector<char> in(n, 0);
        in[static_cast<std::size_t>(p)] = 1;
        for (std::size_t i = 1; i < n; ++i) {
            NodeId x = ctx.nodes[i].parent;
            std::int64_t steps = 0;
            while (x != SINK && x != NONE && x != p && steps <= static_cast<std::int64_t>(n)) {
                x = ctx.nodes[static_cast<std::size_t>(x)].parent;
                ++steps;
            }
            if (x == p) in[i] = 1;
        }
        return in;
    }

    static std::vector<std::int64_t> alive_descendant_counts(const AlgorithmContext& ctx) {
        const auto n = ctx.nodes.size();
        std::vector<std::int64_t> cnt(n, 0);
        for (std::size_t i = 1; i < n; ++i) {
            if (!ctx.nodes[i].alive) continue;
            NodeId x = ctx.nodes[i].parent;
            std::int64_t steps = 0;
            while (x != SINK && x != NONE && steps <= static_cast<std::int64_t>(n)) {
                if (!ctx.nodes[static_cast<std::size_t>(x)].alive) break;
                ++cnt[static_cast<std::size_t>(x)];
                x = ctx.nodes[static_cast<std::size_t>(x)].parent;
                ++steps;
            }
        }
        return cnt;
    }

    std::int64_t coverage(const AlgorithmContext& ctx, NodeId c, const std::vector<NodeId>& S) const {
        std::int64_t cov = 0;
        for (NodeId k : S) if (k != c && in_range(ctx, k, c)) ++cov;
        return cov;
    }

    Plan evaluate(const AlgorithmContext& ctx, NodeId p, const std::vector<NodeId>& S, NodeId g, NodeId only_c = NONE) {
        const bool full = only_c == NONE;
        if (full) ++evaluations_;
        Plan pl;

        // Stage A: child in range of a routed grandparent. Max coverage, then nearest, then id.
        if (g != NONE && routed_now(ctx, g)) {
            std::int64_t best_cov = -1;
            double best_d = 0.0;
            for (NodeId c : S) {
                if (!full && c != only_c) continue;
                if (!in_range(ctx, c, g)) continue;
                const std::int64_t cov = coverage(ctx, c, S);
                const double d = dist(ctx, c, g);
                const bool better = pl.c == NONE || cov > best_cov || (cov == best_cov && d < best_d && !tie(d, best_d));
                if (better) { pl.c = c; best_cov = cov; best_d = d; }
            }
        }
        if (pl.c != NONE) {
            pl.stage = Plan::Stage::A;
            pl.target = g;
            pl.a_empty = false;
            return pl;
        }
        if (full) ++out_of_reach_;

        // Stage B: X = desc(p) + children(g) + {p, g}; q must be routed and outside X.
        std::vector<char> X = descendants_of(ctx, p);
        if (g != NONE) {
            X[static_cast<std::size_t>(g)] = 1;
            for (std::size_t i = 1; i < ctx.nodes.size(); ++i)
                if (ctx.nodes[i].alive && ctx.nodes[i].parent == g) X[i] = 1;
        }
        const std::vector<std::int64_t> desc = alive_descendant_counts(ctx);
        auto len = [&](NodeId x) { return l_ * (1.0 + static_cast<double>(desc[static_cast<std::size_t>(x)])); };

        struct ChildInfo { std::int64_t cov; bool full; double sibling_term; };
        std::vector<ChildInfo> info(S.size());
        for (std::size_t a = 0; a < S.size(); ++a) {
            const NodeId c = S[a];
            std::int64_t cov = 0;
            double term = 0.0;
            for (NodeId k : S) {
                if (k == c || !in_range(ctx, k, c)) continue;
                ++cov;
                const double d = dist(ctx, k, c);
                term += len(k) * d * d;
            }
            info[a] = {cov, cov == static_cast<std::int64_t>(S.size()) - 1, term};
        }

        std::vector<char> has_pair(S.size(), 0);
        bool any_pair = false, any_full = false;
        std::int64_t max_cov = -1;
        for (std::size_t a = 0; a < S.size(); ++a) {
            if (!full && S[a] != only_c) continue;
            for (NodeId q : ctx.nodes[static_cast<std::size_t>(S[a])].nbr) {
                if (X[static_cast<std::size_t>(q)] || !routed_now(ctx, q)) continue;
                has_pair[a] = 1;
                break;
            }
            if (!has_pair[a]) continue;
            any_pair = true;
            if (info[a].full) any_full = true;
            max_cov = std::max(max_cov, info[a].cov);
        }
        if (!any_pair) return pl;

        // Minimise E_new(c,q); ties by distance, then id_c, then id_q.
        double best_E = 0.0, best_d = 0.0;
        for (std::size_t a = 0; a < S.size(); ++a) {
            if (!has_pair[a]) continue;
            if (any_full ? !info[a].full : info[a].cov != max_cov) continue;
            const NodeId c = S[a];
            for (NodeId q : ctx.nodes[static_cast<std::size_t>(c)].nbr) {
                if (X[static_cast<std::size_t>(q)] || !routed_now(ctx, q)) continue;
                const double d = dist(ctx, c, q);
                const double E = eps_fs_ * (len(c) * d * d + info[a].sibling_term);
                bool better;
                if (pl.c == NONE) better = true;
                else if (!tie(E, best_E)) better = E < best_E;
                else better = d < best_d && !tie(d, best_d);
                if (better) { pl.c = c; pl.target = q; best_E = E; best_d = d; }
            }
        }
        SIM_REQUIRE(pl.c != NONE, "frame " << ctx.frame << ": nemcr Stage B found pairs for " << p << " but selected none");
        pl.stage = Plan::Stage::B;
        return pl;
    }

    bool applicable(const AlgorithmContext& ctx, NodeId p, const Plan& pl) const {
        const Node& c = ctx.nodes[static_cast<std::size_t>(pl.c)];
        if (!c.alive || c.parent != p) return false;
        return routed_now(ctx, pl.target);
    }

    void apply(AlgorithmContext& ctx, NodeId p, const std::vector<NodeId>& S, const Plan& pl,
               std::int64_t& attached, std::int64_t& orphaned, bool& inherited, std::int64_t& new_violations) {
        if (pl.stage == Plan::Stage::None) {
            for (NodeId k : S) links_->row(ctx.frame, k, p, NONE);
            orphaned = static_cast<std::int64_t>(S.size());
            return;
        }
        SIM_REQUIRE(in_range(ctx, pl.c, pl.target), "frame " << ctx.frame << ": nemcr link " << pl.c << " -> " << pl.target
                                                            << " is " << dist(ctx, pl.c, pl.target) << " m > R_max");
        ctx.set_parent(pl.c, pl.target);
        links_->row(ctx.frame, pl.c, p, pl.target);
        ++attached;
        for (NodeId k : S) {
            if (k == pl.c) continue;
            if (in_range(ctx, k, pl.c)) {
                ctx.set_parent(k, pl.c);
                links_->row(ctx.frame, k, p, pl.c);
                ++attached;
            } else {
                links_->row(ctx.frame, k, p, NONE);
                ++orphaned;
            }
        }
        if (inherit_.on) {
            new_violations = ctx.inherit_slot(pl.c, p);
            inherited = true;
        }
    }
};

}

std::unique_ptr<Algorithm> make_nemcr_arm(const Config& cfg) { return std::make_unique<NemcrArm>(cfg); }

}
