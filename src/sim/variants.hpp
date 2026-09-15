#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model/energy.hpp"
#include "model/geometry.hpp"
#include "model/graph.hpp"
#include "sim/neighborview.hpp"

namespace sim::variants {

using TreeBuilder = graph::Tree (*)(const std::vector<geo::Point>& pos, const graph::Adjacency& adj_gc);

using SlotAssigner = std::vector<std::int32_t> (*)(const graph::Tree& tree, const graph::Adjacency& adj_gi,
                                                   std::int32_t& K);

struct TopologyProfile {
    std::string name;
    TreeBuilder tree;
    SlotAssigner slots;
    bool slots_unique;      // true under `paper`
};

// Under `paper` the tree is EERA and tree_rule is ignored.
const TopologyProfile& topology_profile(const std::string& profile, const std::string& tree_rule);

// free: CTRL entries cost nothing. costed: priced like data.
const energy::ControlCostModel& control_cost_model(const std::string& name);

// quadratic: 1 - (d/R_max)^2. linear: 1 - d/R_max.
using ProximityFn = double (*)(double d, double R_max);
ProximityFn proximity(const std::string& name);

const KnowledgeMode& knowledge_mode(const std::string& name);

// once: elect the takeover child at setup, find its attachment at the death.
// frozen: fix child and target at setup.
// on_failure: run the whole procedure at the death.
enum class RefreshMode { Once, Frozen, OnFailure };
struct BackupRefresh {
    std::string name;
    RefreshMode mode;
};
const BackupRefresh& nemcr_backup_refresh(const std::string& name);

struct Backups {
    std::string name;
    bool on;
};
const Backups& radpr_backups(const std::string& name);

struct SlotInherit {
    std::string name;
    bool on;
};
const SlotInherit& nemcr_slot_inherit(const std::string& name);

// sender: the chooser's own state. neighbour: the candidate's.
struct CostAttribution {
    std::string name;
    bool neighbour;
};
const CostAttribution& dcfr_cost_attribution(const std::string& name);

// eq13_14: pi - (pi/4) x. eq9_10: pi - (pi/2) x.
using EnergyMapFn = double (*)(double x);
EnergyMapFn escfr_energy_map(const std::string& name);

// Scope of R_rate: neighbourhood, network-wide running max, or P_max.
enum class RateScope { Neighbourhood, Network, Calibration };
struct RateNormaliser {
    std::string name;
    RateScope scope;
};
const RateNormaliser& dcfr_rate_normaliser(const std::string& name);

using RateWindowFn = std::int64_t (*)(std::int64_t period, std::int64_t M);
RateWindowFn dcfr_rate_window(const std::string& name);

using PeriodFn = std::int64_t (*)(std::int64_t M);
PeriodFn dcfr_period(const std::string& name);

// pri: the PRI score. path_cost / fa_cost: the negated path total, so the
// higher-is-better machinery runs unchanged.
struct ScoreRule {
    std::string name;
    bool path_cost;
    bool fa;
};
const ScoreRule& radpr_score(const std::string& name);

// off drops MTC_j from the hop total.
struct PathAccumulation {
    std::string name;
    bool on;
};
const PathAccumulation& dcfr_path_accumulation(const std::string& name);

// hold: keep forwarding to the routeless parent.
// detach: par = NONE at once.
// detach_signal: detach in S6 from a route bit learnt from the parent.
struct Stranded {
    std::string name;
    bool detach;
    bool signal;
};
const Stranded& radpr_stranded(const std::string& name);

}
