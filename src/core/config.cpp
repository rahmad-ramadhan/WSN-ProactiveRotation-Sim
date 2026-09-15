#include "core/config.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "core/assertions.hpp"
#include "core/format.hpp"
#include "core/version.hpp"

namespace sim {

namespace {

KeyDef Int(const char* name, std::int64_t Config::*f, const char* doc) {
    KeyDef k; k.name = name; k.type = KeyType::Int; k.field = f; k.doc = doc; return k;
}
KeyDef Real(const char* name, double Config::*f, const char* doc) {
    KeyDef k; k.name = name; k.type = KeyType::Real; k.field = f; k.doc = doc; return k;
}
KeyDef Bool(const char* name, bool Config::*f, const char* doc) {
    KeyDef k; k.name = name; k.type = KeyType::Bool; k.field = f; k.doc = doc; return k;
}
KeyDef Str(const char* name, std::string Config::*f, const char* doc) {
    KeyDef k; k.name = name; k.type = KeyType::Str; k.field = f; k.doc = doc; return k;
}
KeyDef Enum(const char* name, std::string Config::*f, std::vector<std::string> options,
            const char* doc) {
    KeyDef k; k.name = name; k.type = KeyType::Enum; k.field = f; k.options = std::move(options);
    k.doc = doc; return k;
}

KeyDef required(KeyDef k)   { k.required = true; return k; }
KeyDef unhashed(KeyDef k)   { k.hashed = false; return k; }
KeyDef ge(KeyDef k, double lo) { k.lo = lo; k.lo_open = false; return k; }
KeyDef gt(KeyDef k, double lo) { k.lo = lo; k.lo_open = true; return k; }
KeyDef le(KeyDef k, double hi) { k.hi = hi; k.hi_open = false; return k; }
KeyDef one_of(KeyDef k, std::vector<std::int64_t> vals) { k.int_options = std::move(vals); return k; }

// Table order is echo order and hash order: append only.

std::vector<KeyDef> build_table() {
    std::vector<KeyDef> t;

    // run identity
    t.push_back(required(Str("arm", &Config::arm,
        "algorithm to run; validated against the arm registry, never defaulted")));
    t.push_back(unhashed(required(ge(Int("seed", &Config::seed,
        "deployment seed; positions seeded with rho*1000+seed; a label when --positions is given"), 0))));
    t.push_back(unhashed(Str("run_id", &Config::run_id,
        "run label; empty derives <arm>_rho<rho>_s<seed>")));
    t.push_back(unhashed(Str("positions", &Config::positions,
        "positions CSV (id,x,y); replaces the random draw entirely")));
    t.push_back(unhashed(Bool("fast", &Config::fast,
        "skip the O(N^2) invariant checks")));

    // deployment and physics
    t.push_back(ge(Int("N", &Config::N, "sensor node count, ids 1..N; the sink is id 0"), 1));
    t.push_back(one_of(Int("rho", &Config::rho, "density in nodes/km^2; fixes the field side L"), {400, 600, 800, 1000}));
    t.push_back(gt(Real("R_max", &Config::R_max, "communication range, metres"), 0));
    t.push_back(gt(Real("R_int", &Config::R_int, "interference range, metres; the given is exactly 2*R_max"), 0));
    t.push_back(gt(Real("E_elec", &Config::E_elec, "electronics energy, J/bit"), 0));
    t.push_back(gt(Real("eps_fs", &Config::eps_fs, "free-space amplifier energy, J/bit/m^2"), 0));
    t.push_back(ge(Int("l_data", &Config::l_data, "payload bits per datum"), 1));
    t.push_back(gt(Real("T_slot", &Config::T_slot, "slot length, seconds; reporting only, no algorithm reads it"), 0));
    t.push_back(ge(Int("l_ctrl", &Config::l_ctrl, "control message bits"), 1));

    // calibration and run length
    t.push_back(le(gt(Real("kappa", &Config::kappa, "calibration fraction; L_ref = kappa*T_run"), 0), 1));
    t.push_back(ge(Int("T_run", &Config::T_run, "calibration horizon in frames"), 1));
    t.push_back(ge(Int("T_stop", &Config::T_stop, "hard frame cap"), 1));
    t.push_back(le(gt(Real("alpha", &Config::alpha, "EWMA weight for P_hat, updated every frame"), 0), 1));

    // trace output, not hashed
    t.push_back(unhashed(ge(Int("trace_frames", &Config::trace_frames,
        "frames of trace.csv to write; 0 writes no trace"), 0)));
    t.push_back(unhashed(ge(Int("trace_every", &Config::trace_every,
        "trace every k-th frame"), 1)));
    t.push_back(unhashed(le(ge(Real("trace_min_routed", &Config::trace_min_routed,
        "stop tracing once the routed fraction of nodes falls below this"), 0), 1)));
    t.push_back(unhashed(Bool("log_gaps", &Config::log_gaps,
        "write gaps.csv, one row per radpr parent evaluation; off saves ~20 MB per full run")));
    t.push_back(unhashed(ge(Int("dcfr_log_frames", &Config::dcfr_log_frames,
        "frames of dcfr_decisions.csv to write, one row per dcfr/escfr routing decision; 0 writes none"), 0)));

    // radpr
    t.push_back(ge(Real("w1", &Config::w1, "PRI weight on residual fraction; w1+w2+w3 = 1"), 0));
    t.push_back(ge(Real("w2", &Config::w2, "PRI weight on trajectory term R"), 0));
    t.push_back(ge(Real("w3", &Config::w3, "PRI weight on proximity"), 0));
    t.push_back(ge(Int("M", &Config::M, "radpr re-evaluation and broadcast period, frames"), 1));
    t.push_back(gt(Real("beta", &Config::beta, "horizon fraction; T_horizon = beta*L_ref"), 0));
    t.push_back(ge(Real("Gamma", &Config::Gamma, "radpr switching margin: a candidate must beat the current parent by more than this"), 0));
    t.push_back(ge(Int("k", &Config::k, "nodes one backup may serve; read only under radpr_backups = on"), 0));
    t.push_back(ge(Int("T_orphan", &Config::T_orphan, "ParentRequest collection window, frames"), 0));

    // dcfr / escfr
    t.push_back(gt(Real("dcfr_sin_eps", &Config::dcfr_sin_eps, "clamp of the sin argument away from pi, radians"), 0));
    t.push_back(gt(Real("dcfr_cost_cap", &Config::dcfr_cost_cap, "cap on any single path cost"), 0));

    // variant keys
    t.push_back(Enum("dcfr_cost_attribution", &Config::dcfr_cost_attribution, {"sender", "neighbour"},
        "whose state weights a hop cost"));
    t.push_back(Enum("escfr_energy_map", &Config::escfr_energy_map, {"eq13_14", "eq9_10"},
        "residual-energy to angle map; eq13_14 is Liu's improved form"));
    t.push_back(Enum("dcfr_rate_normaliser", &Config::dcfr_rate_normaliser, {"neighbourhood", "network", "calibration"},
        "scope of R_rate in Liu Eq. 16"));
    t.push_back(Enum("dcfr_rate_window", &Config::dcfr_rate_window, {"period", "1", "M"},
        "window W of Liu Eq. 15, in frames"));
    t.push_back(Enum("dcfr_period", &Config::dcfr_period, {"1", "M"},
        "dcfr/escfr broadcast and re-evaluation period"));
    t.push_back(Enum("nemcr_backup_refresh", &Config::nemcr_backup_refresh, {"once", "frozen", "on_failure"},
        "once: takeover child fixed at setup, attachment point looked up at failure; frozen: whole plan fixed at setup; on_failure: all live"));
    t.push_back(Enum("radpr_proximity", &Config::radpr_proximity, {"quadratic", "linear"},
        "shape of the proximity term, normalised on R_max"));
    t.push_back(Enum("radpr_backups", &Config::radpr_backups, {"off", "on"},
        "whether radpr keeps a stored backup parent per node"));
    t.push_back(Enum("nemcr_slot_inherit", &Config::nemcr_slot_inherit, {"on", "off"},
        "takeover node inherits the dead parent's slot"));
    t.push_back(Enum("topology_profile", &Config::topology_profile, {"shared", "paper"},
        "shared: min-hop tree + interference colouring; paper: EERA tree + tree-ordered slots (run-level)"));
    t.push_back(Enum("precedence_rule", &Config::precedence_rule, {"depth", "path_vector", "sink_distance"},
        "order used to keep parent switches cycle-free"));
    t.push_back(Enum("control_cost_model", &Config::control_cost_model, {"free", "costed"},
        "whether control messages cost energy"));
    t.push_back(Enum("knowledge_mode", &Config::knowledge_mode, {"explicit", "oracle"},
        "explicit: stale views between broadcasts; oracle: true current state"));
    t.push_back(Enum("tree_rule", &Config::tree_rule, {"minhop"},
        "initial tree rule under the shared profile"));
    t.push_back(Enum("radpr_score", &Config::radpr_score, {"pri", "path_cost", "fa_cost"},
        "pri: the RA-DPR priority score; path_cost: ESCFR's accumulated cost, negated, inside radpr's framework; fa_cost: the fa arm's power-law cost (fa_x1, fa_x2), likewise"));
    t.push_back(Enum("dcfr_path_accumulation", &Config::dcfr_path_accumulation, {"on", "off"},
        "on: total = C_ij [+ RC_ij] + MTC_j; off: MTC_j dropped from the total (still the route test)"));
    t.push_back(Enum("radpr_stranded", &Config::radpr_stranded, {"hold", "detach", "detach_signal"},
        "radpr node alive with an alive parent and no route: hold (keep forwarding until the periodic check), detach (drop the parent as soon as the route is lost), "
        "or detach_signal (no route oracle: a one-bit known_route flag learnt from the parent one hop per frame, detach when it goes false)"));
    // fa
    t.push_back(Enum("pm_period", &Config::pm_period, {"1", "M"},
        "broadcast and re-evaluation period of the path-metric arms: 1 or M frames"));
    t.push_back(gt(Real("fa_x1", &Config::fa_x1, "Chang-Tassiulas exponent on link energy"), 0));
    t.push_back(ge(Real("fa_x2", &Config::fa_x2, "Chang-Tassiulas exponent on residual fraction (steepness)"), 0));

    return t;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) s += ", "; s += v[i]; }
    return s;
}
std::string join(const std::vector<std::int64_t>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) s += ", "; s += std::to_string(v[i]); }
    return s;
}

std::string range_text(const KeyDef& k) {
    if (!k.lo && !k.hi) return "";
    std::string s;
    if (k.lo) s += (k.lo_open ? "> " : ">= ") + fmt::canonical(*k.lo);
    if (k.hi) { if (!s.empty()) s += " and "; s += (k.hi_open ? "< " : "<= ") + fmt::canonical(*k.hi); }
    return s;
}

std::string default_text(const KeyDef& k) {
    static const Config defaults{};
    return key_value_text(defaults, k);
}

}

const char* key_type_name(KeyType t) {
    switch (t) {
        case KeyType::Int: return "integer";
        case KeyType::Real: return "number";
        case KeyType::Bool: return "boolean";
        case KeyType::Str: return "string";
        case KeyType::Enum: return "string";
    }
    return "?";
}

const std::vector<KeyDef>& config_keys() {
    static const std::vector<KeyDef> table = build_table();
    return table;
}

const KeyDef* find_key(const std::string& name) {
    for (const auto& k : config_keys())
        if (k.name == name) return &k;
    return nullptr;
}

std::string key_value_text(const Config& cfg, const KeyDef& key) {
    switch (key.type) {
        case KeyType::Int:  return fmt::canonical(cfg.*std::get<std::int64_t Config::*>(key.field));
        case KeyType::Real: return fmt::canonical(cfg.*std::get<double Config::*>(key.field));
        case KeyType::Bool: return fmt::canonical(cfg.*std::get<bool Config::*>(key.field));
        case KeyType::Str:
        case KeyType::Enum: return cfg.*std::get<std::string Config::*>(key.field);
    }
    return "";
}

std::set<std::string> config_load_json(Config& cfg, const json::Value& root, const std::string& source) {
    SIM_REQUIRE(root.kind == json::Value::Kind::Object,
                source << ": the config must be a JSON object, got " << root.kind_name());
    std::set<std::string> provided;
    for (const auto& [name, v] : root.object) {
        if (name == "resolved") continue;  // echo output, not an input
        const KeyDef* key = find_key(name);
        if (!key) {
            std::vector<std::string> names;
            for (const auto& k : config_keys()) names.push_back(k.name);
            SIM_FAIL(source << ": unknown config key \"" << name << "\"; valid keys: " << join(names));
        }
        switch (key->type) {
            case KeyType::Int: {
                SIM_REQUIRE(v.kind == json::Value::Kind::Number,
                            source << ": key \"" << name << "\" expects an integer, got " << v.kind_name());
                bool integral = v.raw.find_first_of(".eE") == std::string::npos;
                SIM_REQUIRE(integral, source << ": key \"" << name << "\" expects an integer, got " << v.raw);
                char* end = nullptr;
                errno = 0;
                long long x = std::strtoll(v.raw.c_str(), &end, 10);
                SIM_REQUIRE(errno == 0 && end && *end == '\0',
                            source << ": key \"" << name << "\": integer out of range: " << v.raw);
                cfg.*std::get<std::int64_t Config::*>(key->field) = x;
                break;
            }
            case KeyType::Real:
                SIM_REQUIRE(v.kind == json::Value::Kind::Number,
                            source << ": key \"" << name << "\" expects a number, got " << v.kind_name());
                cfg.*std::get<double Config::*>(key->field) = v.number;
                break;
            case KeyType::Bool:
                SIM_REQUIRE(v.kind == json::Value::Kind::Bool,
                            source << ": key \"" << name << "\" expects true or false, got " << v.kind_name());
                cfg.*std::get<bool Config::*>(key->field) = v.boolean;
                break;
            case KeyType::Str:
                SIM_REQUIRE(v.kind == json::Value::Kind::String,
                            source << ": key \"" << name << "\" expects a string, got " << v.kind_name());
                cfg.*std::get<std::string Config::*>(key->field) = v.str;
                break;
            case KeyType::Enum:
                SIM_REQUIRE(v.kind == json::Value::Kind::String,
                            source << ": key \"" << name << "\" expects a string, one of {" << join(key->options)
                                   << "}, got " << v.kind_name());
                cfg.*std::get<std::string Config::*>(key->field) = v.str;
                break;
        }
        provided.insert(name);
    }
    return provided;
}

void config_validate(const Config& cfg, const std::set<std::string>& provided) {
    for (const auto& key : config_keys()) {
        if (key.required)
            SIM_REQUIRE(provided.contains(key.name),
                        "config key \"" << key.name << "\" is required and has no default (" << key.doc << ")");

        switch (key.type) {
            case KeyType::Int: {
                std::int64_t x = cfg.*std::get<std::int64_t Config::*>(key.field);
                if (!key.int_options.empty()) {
                    bool ok = false;
                    for (auto v : key.int_options) if (v == x) ok = true;
                    SIM_REQUIRE(ok, "config key \"" << key.name << "\" = " << x
                                    << " is not one of {" << join(key.int_options) << "}");
                }
                auto xd = static_cast<double>(x);
                if (key.lo) SIM_REQUIRE(key.lo_open ? xd > *key.lo : xd >= *key.lo,
                                        "config key \"" << key.name << "\" = " << x << " must be " << range_text(key));
                if (key.hi) SIM_REQUIRE(key.hi_open ? xd < *key.hi : xd <= *key.hi,
                                        "config key \"" << key.name << "\" = " << x << " must be " << range_text(key));
                break;
            }
            case KeyType::Real: {
                double x = cfg.*std::get<double Config::*>(key.field);
                SIM_REQUIRE(std::isfinite(x), "config key \"" << key.name << "\" is not finite");
                if (key.lo) SIM_REQUIRE(key.lo_open ? x > *key.lo : x >= *key.lo,
                                        "config key \"" << key.name << "\" = " << fmt::canonical(x) << " must be " << range_text(key));
                if (key.hi) SIM_REQUIRE(key.hi_open ? x < *key.hi : x <= *key.hi,
                                        "config key \"" << key.name << "\" = " << fmt::canonical(x) << " must be " << range_text(key));
                break;
            }
            case KeyType::Enum: {
                const std::string& x = cfg.*std::get<std::string Config::*>(key.field);
                bool ok = false;
                for (const auto& o : key.options) if (o == x) ok = true;
                SIM_REQUIRE(ok, "config key \"" << key.name << "\" = \"" << x
                                << "\" is not one of {" << join(key.options) << "}");
                break;
            }
            case KeyType::Bool:
            case KeyType::Str:
                break;
        }
    }

    // cross-key rules
    SIM_REQUIRE(!cfg.arm.empty(), "config key \"arm\" must not be empty");
    SIM_REQUIRE(cfg.R_int >= cfg.R_max,
                "R_int (" << fmt::canonical(cfg.R_int) << ") must be >= R_max (" << fmt::canonical(cfg.R_max)
                          << "): a child and its parent must be adjacent in G_I");
    double wsum = cfg.w1 + cfg.w2 + cfg.w3;
    SIM_REQUIRE(std::fabs(wsum - 1.0) <= 1e-9,
                "w1 + w2 + w3 must equal 1; got " << fmt::canonical(wsum));
}

Resolved config_resolve(const Config& cfg) {
    Resolved r;
    r.run_id = cfg.run_id.empty()
        ? cfg.arm + "_rho" + std::to_string(cfg.rho) + "_s" + std::to_string(cfg.seed)
        : cfg.run_id;
    // rho is per km^2; L is in metres
    r.L = 1000.0 * std::sqrt(static_cast<double>(cfg.N) / static_cast<double>(cfg.rho));
    r.L_ref = cfg.kappa * static_cast<double>(cfg.T_run);
    r.T_horizon = cfg.beta * r.L_ref;
    r.config_hash = config_hash(cfg);
    r.sim_version = SIM_VERSION;
    return r;
}

std::string config_hash(const Config& cfg) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto& key : config_keys()) {
        if (!key.hashed) continue;
        h = fmt::fnv1a64(key.name + "=" + key_value_text(cfg, key) + "\n", h);
    }
    return fmt::hex64(h);
}

std::string config_echo(const Config& cfg, const Resolved& res) {
    std::ostringstream os;
    os << "{\n";
    for (const auto& key : config_keys()) {
        os << "  " << fmt::json_string(key.name) << ": ";
        if (key.type == KeyType::Str || key.type == KeyType::Enum) os << fmt::json_string(key_value_text(cfg, key));
        else os << key_value_text(cfg, key);
        os << ",\n";
    }
    os << "  \"resolved\": {\n";
    os << "    \"run_id\": " << fmt::json_string(res.run_id) << ",\n";
    os << "    \"L\": " << fmt::canonical(res.L) << ",\n";
    os << "    \"L_ref\": " << fmt::canonical(res.L_ref) << ",\n";
    os << "    \"T_horizon\": " << fmt::canonical(res.T_horizon) << ",\n";
    os << "    \"config_hash\": " << fmt::json_string(res.config_hash) << ",\n";
    os << "    \"sim_version\": " << fmt::json_string(res.sim_version) << "\n";
    os << "  }\n";
    os << "}\n";
    return os.str();
}

std::string config_help() {
    std::ostringstream os;
    os << "Config keys (JSON object in --config). Every key is optional except those marked required.\n"
       << "Keys marked [cell] enter the config hash that identifies a campaign cell.\n\n";
    for (const auto& key : config_keys()) {
        os << "  " << key.name << "  (" << key_type_name(key.type);
        if (key.required) os << ", required";
        else os << ", default " << default_text(key);
        if (key.type == KeyType::Enum) os << "; one of {" << join(key.options) << "}";
        if (!key.int_options.empty()) os << "; one of {" << join(key.int_options) << "}";
        std::string r = range_text(key);
        if (!r.empty()) os << "; " << r;
        os << ")" << (key.hashed ? " [cell]" : "") << "\n";
        os << "      " << key.doc << "\n";
    }
    return os.str();
}

}
