#include "io/metrics.hpp"

#include <fstream>
#include <limits>

#include "core/assertions.hpp"
#include "core/format.hpp"

namespace sim {

namespace {

// Append new columns at the end; do not reorder.

std::vector<ColumnDef> build_columns() {
    std::vector<ColumnDef> c;
    auto add = [&](const char* name, ColType t, const char* source) { c.push_back({name, t, source, std::nullopt}); };
    // Arm-specific columns: the value written when another arm runs.
    auto add_d = [&](const char* name, ColType t, const char* source, MetricValue d) { c.push_back({name, t, source, d}); };
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // identity
    add("run_id", ColType::Str, "resolved");
    add("arm", ColType::Str, "config");
    add("rho", ColType::Int, "config");
    add("seed", ColType::Int, "config");
    add("N", ColType::Int, "config");
    add("R_max", ColType::Real, "config");
    add("R_int", ColType::Real, "config");
    add("L", ColType::Real, "resolved");

    // parameters
    add("w1", ColType::Real, "config");
    add("w2", ColType::Real, "config");
    add("w3", ColType::Real, "config");
    add("M", ColType::Int, "config");
    add("beta", ColType::Real, "config");
    add("alpha", ColType::Real, "config");
    add("kappa", ColType::Real, "config");
    add("Gamma", ColType::Real, "config");
    add("k", ColType::Int, "config");
    add("l_ctrl", ColType::Int, "config");
    add("dcfr_cost_attribution", ColType::Str, "config");
    add("escfr_energy_map", ColType::Str, "config");
    add("dcfr_rate_normaliser", ColType::Str, "config");
    add("dcfr_rate_window", ColType::Str, "config");
    add("dcfr_period", ColType::Str, "config");
    add("nemcr_backup_refresh", ColType::Str, "config");
    add("radpr_proximity", ColType::Str, "config");
    add("nemcr_slot_inherit", ColType::Str, "config");
    add("topology_profile", ColType::Str, "config");
    add("precedence_rule", ColType::Str, "config");
    add("control_cost_model", ColType::Str, "config");
    add("knowledge_mode", ColType::Str, "config");
    add("tree_rule", ColType::Str, "config");

    // resolved
    add("E_0", ColType::Real, "setup");
    add("P_max", ColType::Real, "setup");
    add("L_ref", ColType::Real, "resolved");
    add("T_horizon", ColType::Real, "resolved");
    add("K", ColType::Int, "setup");
    add("T_frame", ColType::Real, "setup");
    add("tree_depth_max", ColType::Int, "setup");
    add("deployment_rejections", ColType::Int, "setup");

    // topology
    add("mean_degree_gc", ColType::Real, "setup");
    add("max_degree_gi", ColType::Int, "setup");

    // lifetime; frame columns read `not reached` if censored
    add("fnd_frame", ColType::Frame, "S4");          // first residual <= 0
    add("t_1pct", ColType::Frame, "S4");             // dead count reaches ceil(1% of N)
    add("t_10pct", ColType::Frame, "S4");
    add("t_50pct", ColType::Frame, "S4");
    add("partition_frame", ColType::Frame, "S5");    // no routed alive node
    add("adt_frame", ColType::Frame, "S4");          // all nodes dead
    add("real_depth_max", ColType::Int, "S5");

    // delivery
    add("generated_total", ColType::Int, "S1");
    add("delivered_total", ColType::Int, "S2");
    add("lost_orphan_total", ColType::Int, "S2");
    add("lost_death_total", ColType::Int, "S5");
    add("held_at_stop", ColType::Int, "S8");
    add("pdr_global", ColType::Real, "S8");          // delivered / generated
    add("pdr_to_fnd", ColType::Real, "S7");          // same ratio up to fnd_frame

    // balance
    add("jain_index_fnd", ColType::Real, "S7");
    add("jain_index_final", ColType::Real, "S8");
    add("energy_total", ColType::Real, "ledger");
    add("energy_distance_component", ColType::Real, "ledger");

    // control
    add("control_msgs", ColType::Int, "S3");
    add("control_bits", ColType::Int, "S3");
    add("control_bits_frac", ColType::Real, "S3");
    add("control_energy", ColType::Real, "ledger");

    // radpr rotation
    add_d("rotations_total", ColType::Int, "arm:radpr", std::int64_t{0});   // S6 switches
    add_d("gap_evaluations", ColType::Int, "arm:radpr", std::int64_t{0});
    add_d("gap_p05", ColType::Real, "arm:radpr", nan);                      // nearest-rank percentiles of best - incumbent
    add_d("gap_p50", ColType::Real, "arm:radpr", nan);
    add_d("gap_p95", ColType::Real, "arm:radpr", nan);
    add_d("gap_above_gamma", ColType::Int, "arm:radpr", std::int64_t{0});

    // recovery
    add("orphan_frames_total", ColType::Int, "S7");
    add("orphan_nodes_ever", ColType::Int, "S7");
    add_d("recoveries_backup", ColType::Int, "arm:radpr", std::int64_t{0});
    add_d("recoveries_select", ColType::Int, "arm:radpr", std::int64_t{0});
    add_d("recoveries_request", ColType::Int, "arm:radpr", std::int64_t{0});
    add("recovery_failures", ColType::Int, "S5");    // children orphaned after the arm's S5 recovery
    add_d("rejoins", ColType::Int, "arm:radpr", std::int64_t{0});
    add_d("requests_sent", ColType::Int, "arm:radpr", std::int64_t{0});
    add_d("backups_assigned", ColType::Int, "arm:radpr", std::int64_t{0});

    // arm-specific
    add("static_order_violations", ColType::Int, "S5");   // I5 links, counted once when they appear
    // nemcr
    add_d("nemcr_out_of_reach_frac", ColType::Real, "arm:nemcr", nan);
    add("slot_inheritances", ColType::Int, "S5");
    add("colouring_violations", ColType::Int, "S5");
    add_d("nemcr_evaluations", ColType::Int, "arm:nemcr", std::int64_t{0});
    add_d("nemcr_stage_a", ColType::Int, "arm:nemcr", std::int64_t{0});
    add_d("nemcr_stage_b", ColType::Int, "arm:nemcr", std::int64_t{0});
    add_d("nemcr_unrecoverable", ColType::Int, "arm:nemcr", std::int64_t{0});
    add_d("nemcr_stale_plans", ColType::Int, "arm:nemcr", std::int64_t{0});
    // dcfr / escfr
    add_d("dcfr_cost_max", ColType::Real, "arm:dcfr", nan);
    add_d("dcfr_nonfinite_costs", ColType::Int, "arm:dcfr", std::int64_t{0});
    add_d("dcfr_cost_capped", ColType::Int, "arm:dcfr", std::int64_t{0});
    add_d("dcfr_rule1_bindings", ColType::Int, "arm:dcfr", std::int64_t{0});
    add_d("dcfr_rule1_active", ColType::Int, "arm:dcfr", std::int64_t{0});
    add_d("dcfr_decisions", ColType::Int, "arm:dcfr", std::int64_t{0});
    add_d("dcfr_no_candidate", ColType::Int, "arm:dcfr", std::int64_t{0});
    add_d("dcfr_mtc_max", ColType::Real, "arm:dcfr", nan);
    add("parent_changes_total", ColType::Int, "S5/S6");

    // diagnostics
    add("i14_frames_checked", ColType::Int, "S7");
    add("i16_max_B", ColType::Int, "S2");
    add("i16_max_slot_bits", ColType::Int, "S2");
    add("stop_frame", ColType::Int, "S8");
    add("stop_reason", ColType::Str, "S8");
    add("trace_stop_reason", ColType::Str, "trace");
    add("trace_frames_written", ColType::Int, "trace");
    add("wall_clock_s", ColType::Real, "main");
    add("sim_version", ColType::Str, "resolved");
    add("config_hash", ColType::Str, "resolved");

    add("radpr_backups", ColType::Str, "config");
    add("orphanings_total", ColType::Int, "S5/S6");                            // parent writes to NONE
    add_d("rotations_routeless", ColType::Int, "arm:radpr", std::int64_t{0});  // switches with the margin waived
    add("t_unreach_25", ColType::Frame, "S5");                                 // dead + orphaned first reaches 25% of N
    add("t_unreach_50", ColType::Frame, "S5");
    add("t_unreach_75", ColType::Frame, "S5");
    add("radpr_score", ColType::Str, "config");
    add("dcfr_path_accumulation", ColType::Str, "config");
    add("energy_stranded", ColType::Real, "S4");                                // data energy on alive routeless nodes that still have a parent
    add("energy_parentless", ColType::Real, "S4");                              // data energy on alive nodes with no parent
    add("radpr_stranded", ColType::Str, "config");
    add_d("radpr_detached", ColType::Int, "arm:radpr", std::int64_t{0});
    // fa
    add("pm_period", ColType::Str, "config");
    add("fa_x1", ColType::Real, "config");
    add("fa_x2", ColType::Real, "config");
    add_d("pm_decisions", ColType::Int, "arm:pathmetric", std::int64_t{0});
    add_d("pm_no_candidate", ColType::Int, "arm:pathmetric", std::int64_t{0});
    add_d("pm_capped", ColType::Int, "arm:pathmetric", std::int64_t{0});
    add_d("pm_metric_max", ColType::Real, "arm:pathmetric", nan);

    return c;
}

const char* col_type_name(ColType t) {
    switch (t) {
        case ColType::Int: return "integer";
        case ColType::Real: return "real";
        case ColType::Str: return "string";
        case ColType::Frame: return "frame";
    }
    return "?";
}

}

const std::vector<ColumnDef>& summary_columns() {
    static const std::vector<ColumnDef> cols = build_columns();
    return cols;
}

const ColumnDef* find_column(const std::string& name) {
    for (const auto& c : summary_columns())
        if (c.name == name) return &c;
    return nullptr;
}

void MetricsSink::check(const std::string& name, ColType t) const {
    const ColumnDef* c = find_column(name);
    SIM_REQUIRE(c != nullptr, "metric \"" << name << "\" is not a registered summary column");
    SIM_REQUIRE(c->type == t, "metric \"" << name << "\" is registered as " << col_type_name(c->type)
                              << " but was set as " << col_type_name(t));
}

void MetricsSink::set(const std::string& name, std::int64_t v) { check(name, ColType::Int); values_[name] = v; }
void MetricsSink::set(const std::string& name, double v) { check(name, ColType::Real); values_[name] = v; }
void MetricsSink::set(const std::string& name, const std::string& v) { check(name, ColType::Str); values_[name] = v; }

void MetricsSink::set_frame(const std::string& name, std::optional<std::int64_t> f) {
    check(name, ColType::Frame);
    if (f) values_[name] = *f;
    else values_[name] = std::string(NOT_REACHED);
}

std::string MetricsSink::text(const std::string& name) const {
    auto render = [](const Value& val) {
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) return fmt::csv_field(v);
            else return fmt::canonical(v);
        }, val);
    };
    auto it = values_.find(name);
    if (it != values_.end()) return render(it->second);
    const ColumnDef* c = find_column(name);
    SIM_REQUIRE(c != nullptr && c->dflt.has_value(), "summary column \"" << name << "\" was never set and has no declared default");
    return render(*c->dflt);
}

std::string summary_header() {
    std::string s;
    for (const auto& c : summary_columns()) { if (!s.empty()) s += ','; s += c.name; }
    return s + "\n";
}

std::string summary_row(const MetricsSink& sink) {
    std::string s;
    for (const auto& c : summary_columns()) { if (!s.empty()) s += ','; s += sink.text(c.name); }
    return s + "\n";
}

void write_summary_csv(const std::string& path, const MetricsSink& sink) {
    std::ofstream out(path, std::ios::binary);
    SIM_REQUIRE(out.good(), "cannot open " << path << " for writing");
    out << summary_header() << summary_row(sink);
    SIM_REQUIRE(out.good(), "write failed: " << path);
}

}
