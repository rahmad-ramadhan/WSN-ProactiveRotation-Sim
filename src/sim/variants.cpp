#include "sim/variants.hpp"

#include "core/assertions.hpp"

namespace sim::variants {

namespace {

std::vector<std::int32_t> slots_colouring(const graph::Tree&, const graph::Adjacency& adj_gi, std::int32_t& K) {
    return graph::greedy_colouring(adj_gi, K);
}

std::vector<std::int32_t> slots_tree_ordered(const graph::Tree& tree, const graph::Adjacency&, std::int32_t& K) {
    return graph::tree_ordered_slots(tree, K);
}

struct TreeRule {
    std::string name;
    TreeBuilder build;
};

const std::vector<TreeRule>& tree_rules() {
    static const std::vector<TreeRule> table = {
        {"minhop", &graph::minhop_tree},
    };
    return table;
}

std::string names(const std::vector<TreeRule>& t) {
    std::string s;
    for (const auto& r : t) { if (!s.empty()) s += ", "; s += r.name; }
    return s;
}

}

const TopologyProfile& topology_profile(const std::string& profile, const std::string& tree_rule) {
    static const TopologyProfile paper = {"paper", &graph::eera_tree, &slots_tree_ordered, true};
    if (profile == "paper") return paper;
    if (profile == "shared") {
        static std::vector<TopologyProfile> shared;
        if (shared.empty())
            for (const auto& r : tree_rules()) shared.push_back({"shared", r.build, &slots_colouring, false});
        for (std::size_t i = 0; i < tree_rules().size(); ++i)
            if (tree_rules()[i].name == tree_rule) return shared[i];
        SIM_FAIL("unknown tree_rule \"" << tree_rule << "\"; valid: " << names(tree_rules()));
    }
    SIM_FAIL("unknown topology_profile \"" << profile << "\"; valid: shared, paper");
}

const energy::ControlCostModel& control_cost_model(const std::string& name) {
    static const std::vector<energy::ControlCostModel> table = {
        {"free", false},
        {"costed", true},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown control_cost_model \"" << name << "\"; valid: " << valid);
}

namespace {
double prox_quadratic(double d, double R_max) { const double r = d / R_max; return 1.0 - r * r; }
double prox_linear(double d, double R_max) { return 1.0 - d / R_max; }

struct ProximityRule {
    std::string name;
    ProximityFn fn;
};
const std::vector<ProximityRule>& proximity_rules() {
    static const std::vector<ProximityRule> table = {
        {"quadratic", &prox_quadratic},
        {"linear", &prox_linear},
    };
    return table;
}
}

ProximityFn proximity(const std::string& name) {
    for (const auto& r : proximity_rules())
        if (r.name == name) return r.fn;
    std::string valid;
    for (const auto& r : proximity_rules()) { if (!valid.empty()) valid += ", "; valid += r.name; }
    SIM_FAIL("unknown radpr_proximity \"" << name << "\"; valid: " << valid);
}

const KnowledgeMode& knowledge_mode(const std::string& name) {
    static const std::vector<KnowledgeMode> table = {
        {"explicit", false},
        {"oracle", true},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown knowledge_mode \"" << name << "\"; valid: " << valid);
}

const BackupRefresh& nemcr_backup_refresh(const std::string& name) {
    static const std::vector<BackupRefresh> table = {
        {"once", RefreshMode::Once},
        {"frozen", RefreshMode::Frozen},
        {"on_failure", RefreshMode::OnFailure},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown nemcr_backup_refresh \"" << name << "\"; valid: " << valid);
}

const Backups& radpr_backups(const std::string& name) {
    static const std::vector<Backups> table = {
        {"off", false},
        {"on", true},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown radpr_backups \"" << name << "\"; valid: " << valid);
}

const ScoreRule& radpr_score(const std::string& name) {
    static const std::vector<ScoreRule> table = {
        {"pri", false, false},
        {"path_cost", true, false},
        {"fa_cost", true, true},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown radpr_score \"" << name << "\"; valid: " << valid);
}

const PathAccumulation& dcfr_path_accumulation(const std::string& name) {
    static const std::vector<PathAccumulation> table = {
        {"on", true},
        {"off", false},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown dcfr_path_accumulation \"" << name << "\"; valid: " << valid);
}

const Stranded& radpr_stranded(const std::string& name) {
    static const std::vector<Stranded> table = {
        {"hold", false, false},
        {"detach", true, false},
        {"detach_signal", false, true},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown radpr_stranded \"" << name << "\"; valid: " << valid);
}

const SlotInherit& nemcr_slot_inherit(const std::string& name) {
    static const std::vector<SlotInherit> table = {
        {"on", true},
        {"off", false},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown nemcr_slot_inherit \"" << name << "\"; valid: " << valid);
}

const CostAttribution& dcfr_cost_attribution(const std::string& name) {
    static const std::vector<CostAttribution> table = {
        {"sender", false},
        {"neighbour", true},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown dcfr_cost_attribution \"" << name << "\"; valid: " << valid);
}

namespace {
constexpr double PI = 3.14159265358979323846;
double map_eq13_14(double x) { return PI - (PI / 4.0) * x; }   // [3pi/4, pi]
double map_eq9_10(double x) { return PI - (PI / 2.0) * x; }    // [pi/2, pi]

struct EnergyMapRule {
    std::string name;
    EnergyMapFn fn;
};
const std::vector<EnergyMapRule>& energy_map_rules() {
    static const std::vector<EnergyMapRule> table = {
        {"eq13_14", &map_eq13_14},
        {"eq9_10", &map_eq9_10},
    };
    return table;
}
}

EnergyMapFn escfr_energy_map(const std::string& name) {
    for (const auto& r : energy_map_rules())
        if (r.name == name) return r.fn;
    std::string valid;
    for (const auto& r : energy_map_rules()) { if (!valid.empty()) valid += ", "; valid += r.name; }
    SIM_FAIL("unknown escfr_energy_map \"" << name << "\"; valid: " << valid);
}

const RateNormaliser& dcfr_rate_normaliser(const std::string& name) {
    static const std::vector<RateNormaliser> table = {
        {"neighbourhood", RateScope::Neighbourhood},
        {"network", RateScope::Network},
        {"calibration", RateScope::Calibration},
    };
    for (const auto& m : table)
        if (m.name == name) return m;
    std::string valid;
    for (const auto& m : table) { if (!valid.empty()) valid += ", "; valid += m.name; }
    SIM_FAIL("unknown dcfr_rate_normaliser \"" << name << "\"; valid: " << valid);
}

namespace {
std::int64_t window_period(std::int64_t period, std::int64_t) { return period; }
std::int64_t window_one(std::int64_t, std::int64_t) { return 1; }
std::int64_t window_M(std::int64_t, std::int64_t M) { return M; }

struct RateWindowRule {
    std::string name;
    RateWindowFn fn;
};
const std::vector<RateWindowRule>& rate_window_rules() {
    static const std::vector<RateWindowRule> table = {
        {"period", &window_period},
        {"1", &window_one},
        {"M", &window_M},
    };
    return table;
}
}

RateWindowFn dcfr_rate_window(const std::string& name) {
    for (const auto& r : rate_window_rules())
        if (r.name == name) return r.fn;
    std::string valid;
    for (const auto& r : rate_window_rules()) { if (!valid.empty()) valid += ", "; valid += r.name; }
    SIM_FAIL("unknown dcfr_rate_window \"" << name << "\"; valid: " << valid);
}

namespace {
std::int64_t period_one(std::int64_t) { return 1; }
std::int64_t period_M(std::int64_t M) { return M; }

struct PeriodRule {
    std::string name;
    PeriodFn fn;
};
const std::vector<PeriodRule>& period_rules() {
    static const std::vector<PeriodRule> table = {
        {"1", &period_one},
        {"M", &period_M},
    };
    return table;
}
}

PeriodFn dcfr_period(const std::string& name) {
    for (const auto& r : period_rules())
        if (r.name == name) return r.fn;
    std::string valid;
    for (const auto& r : period_rules()) { if (!valid.empty()) valid += ", "; valid += r.name; }
    SIM_FAIL("unknown dcfr_period \"" << name << "\"; valid: " << valid);
}

}
