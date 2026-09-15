#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "core/json.hpp"

namespace sim {

struct Config {
    std::string arm;
    std::int64_t seed = 0;        // label only when a positions file is given
    std::string run_id;           // empty: <arm>_rho<rho>_s<seed>
    std::string positions;        // empty: draw positions
    bool fast = false;            // skip the O(N^2) invariant checks

    std::int64_t N = 400;
    std::int64_t rho = 400;
    double R_max = 80.0;
    double R_int = 160.0;
    double E_elec = 50e-9;
    double eps_fs = 10e-12;
    std::int64_t l_data = 4000;   // payload bits
    double T_slot = 0.010;
    std::int64_t l_ctrl = 200;    // control message bits

    double kappa = 0.6;
    std::int64_t T_run = 10000;
    std::int64_t T_stop = 30000;
    double alpha = 0.2;

    std::int64_t trace_frames = 0;    // 0 = no trace.csv
    std::int64_t trace_every = 1;     // write every k-th frame
    double trace_min_routed = 0.0;    // stop tracing once routed/N falls below this
    bool log_gaps = true;
    std::int64_t dcfr_log_frames = 0;  // 0 = no dcfr_decisions.csv

    double w1 = 1.0 / 3.0;
    double w2 = 1.0 / 3.0;
    double w3 = 1.0 / 3.0;
    std::int64_t M = 20;
    double beta = 0.15;
    double Gamma = 0.10;
    std::int64_t k = 3;
    std::int64_t T_orphan = 2;

    double dcfr_sin_eps = 1e-6;
    double dcfr_cost_cap = 1e12;

    std::string dcfr_cost_attribution = "sender";
    std::string escfr_energy_map = "eq13_14";
    std::string dcfr_rate_normaliser = "neighbourhood";
    std::string dcfr_rate_window = "period";
    std::string dcfr_period = "1";
    std::string nemcr_backup_refresh = "once";
    std::string radpr_proximity = "quadratic";
    std::string radpr_backups = "off";
    std::string nemcr_slot_inherit = "on";
    std::string topology_profile = "shared";
    std::string precedence_rule = "depth";
    std::string control_cost_model = "free";
    std::string knowledge_mode = "explicit";
    std::string tree_rule = "minhop";
    std::string radpr_score = "pri";
    std::string dcfr_path_accumulation = "on";
    std::string radpr_stranded = "hold";

    std::string pm_period = "1";                       // 1 | M
    double fa_x1 = 1.0;                                // link-energy exponent
    double fa_x2 = 10.0;                               // residual-fraction exponent
};

enum class KeyType { Int, Real, Bool, Str, Enum };

struct KeyDef {
    std::string name;
    KeyType type;
    std::variant<std::int64_t Config::*, double Config::*, bool Config::*, std::string Config::*> field;
    bool required = false;
    bool hashed = true;
    std::string doc;

    std::optional<double> lo, hi;
    bool lo_open = false, hi_open = false;
    std::vector<std::string> options;
    std::vector<std::int64_t> int_options;
};

const char* key_type_name(KeyType t);

const std::vector<KeyDef>& config_keys();
const KeyDef* find_key(const std::string& name);

std::string key_value_text(const Config& cfg, const KeyDef& key);

// Unknown keys and wrong types are errors. A "resolved" member is ignored,
// so an echoed config.json loads as input.
std::set<std::string> config_load_json(Config& cfg, const json::Value& root,
                                       const std::string& source);

void config_validate(const Config& cfg, const std::set<std::string>& provided);

struct Resolved {
    std::string run_id;
    double L = 0.0;          // field side, metres
    double L_ref = 0.0;      // kappa * T_run
    double T_horizon = 0.0;  // beta * L_ref
    std::string config_hash;
    std::string sim_version;
};
Resolved config_resolve(const Config& cfg);

// FNV-1a 64 over "name=value\n" for every hashed key, in table order.
std::string config_hash(const Config& cfg);

std::string config_echo(const Config& cfg, const Resolved& res);

std::string config_help();

}
