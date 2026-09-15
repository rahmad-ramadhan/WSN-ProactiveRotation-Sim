// sim --config <file.json> --seed <u64> --out <dir> [--positions <csv>] [--fast]

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "arms/registry.hpp"
#include "core/assertions.hpp"
#include "core/config.hpp"
#include "core/json.hpp"
#include "core/rng.hpp"
#include "core/version.hpp"
#include "io/metrics.hpp"
#include "io/topology.hpp"
#include "sim/network.hpp"
#include "sim/setup.hpp"

namespace {

struct CliArgs {
    std::string config_path;
    std::string out_dir;
    std::string positions;
    bool has_seed = false;
    std::int64_t seed = 0;
    bool fast = false;
    bool help = false;
    bool version = false;
};

void print_usage(std::ostream& os) {
    os << "sim " << sim::SIM_VERSION << " - a TDMA wireless-sensor-network routing and lifetime simulator\n\n"
       << "usage: sim --config <file.json> --seed <u64> --out <dir> [--positions <csv>] [--fast]\n"
       << "       sim --help | --version\n\n"
       << "  --config     JSON object of config keys (see below). \"arm\" is required.\n"
       << "  --seed       deployment seed; overrides \"seed\" in the config file\n"
       << "  --out        output directory; created if missing. Writes config.json, topology.csv, summary.csv\n"
       << "  --positions  positions CSV (id,x,y); replaces the random draw\n"
       << "  --fast       skip the O(N^2) invariant checks\n\n"
       << "Arms: " << sim::arm_names_text() << "\n";
    for (const auto& e : sim::arm_table())
        os << "  " << e.name << (e.factory ? "" : "  [not built yet]") << "\n      " << e.summary << "\n";
    os << "\n" << sim::config_help();
}

CliArgs parse_cli(int argc, char** argv) {
    CliArgs a;
    auto need = [&](int& i, const char* flag) -> std::string {
        SIM_REQUIRE(i + 1 < argc, flag << " needs a value");
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--help" || s == "-h") a.help = true;
        else if (s == "--version") a.version = true;
        else if (s == "--config") a.config_path = need(i, "--config");
        else if (s == "--out") a.out_dir = need(i, "--out");
        else if (s == "--positions") a.positions = need(i, "--positions");
        else if (s == "--fast") a.fast = true;
        else if (s == "--seed") {
            std::string v = need(i, "--seed");
            SIM_REQUIRE(!v.empty() && v.find_first_not_of("0123456789") == std::string::npos,
                        "--seed must be an unsigned integer, got \"" << v << "\"");
            errno = 0;
            unsigned long long u = std::strtoull(v.c_str(), nullptr, 10);
            SIM_REQUIRE(errno == 0 && u <= 9223372036854775807ULL, "--seed out of range: " << v);
            a.seed = static_cast<std::int64_t>(u);
            a.has_seed = true;
        } else {
            SIM_FAIL("unknown argument \"" << s << "\"; try --help");
        }
    }
    return a;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    SIM_REQUIRE(in.good(), "cannot read " << path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);   // binary: LF line endings on every platform
    SIM_REQUIRE(out.good(), "cannot open " << path << " for writing");
    out << text;
    SIM_REQUIRE(out.good(), "write failed: " << path);
}

void publish_config(const sim::Config& cfg, const sim::Resolved& res, sim::MetricsSink& sink) {
    sink.set("run_id", res.run_id);
    sink.set("arm", cfg.arm);
    sink.set("rho", cfg.rho);
    sink.set("seed", cfg.seed);
    sink.set("N", cfg.N);
    sink.set("R_max", cfg.R_max);
    sink.set("R_int", cfg.R_int);
    sink.set("L", res.L);
    sink.set("w1", cfg.w1);
    sink.set("w2", cfg.w2);
    sink.set("w3", cfg.w3);
    sink.set("M", cfg.M);
    sink.set("beta", cfg.beta);
    sink.set("alpha", cfg.alpha);
    sink.set("kappa", cfg.kappa);
    sink.set("Gamma", cfg.Gamma);
    sink.set("k", cfg.k);
    sink.set("l_ctrl", cfg.l_ctrl);
    sink.set("dcfr_cost_attribution", cfg.dcfr_cost_attribution);
    sink.set("escfr_energy_map", cfg.escfr_energy_map);
    sink.set("dcfr_rate_normaliser", cfg.dcfr_rate_normaliser);
    sink.set("dcfr_rate_window", cfg.dcfr_rate_window);
    sink.set("dcfr_period", cfg.dcfr_period);
    sink.set("nemcr_backup_refresh", cfg.nemcr_backup_refresh);
    sink.set("radpr_proximity", cfg.radpr_proximity);
    sink.set("radpr_backups", cfg.radpr_backups);
    sink.set("radpr_score", cfg.radpr_score);
    sink.set("dcfr_path_accumulation", cfg.dcfr_path_accumulation);
    sink.set("radpr_stranded", cfg.radpr_stranded);
    sink.set("pm_period", cfg.pm_period);
    sink.set("fa_x1", cfg.fa_x1);
    sink.set("fa_x2", cfg.fa_x2);
    sink.set("nemcr_slot_inherit", cfg.nemcr_slot_inherit);
    sink.set("topology_profile", cfg.topology_profile);
    sink.set("precedence_rule", cfg.precedence_rule);
    sink.set("control_cost_model", cfg.control_cost_model);
    sink.set("knowledge_mode", cfg.knowledge_mode);
    sink.set("tree_rule", cfg.tree_rule);
    sink.set("L_ref", res.L_ref);
    sink.set("T_horizon", res.T_horizon);
    sink.set("sim_version", res.sim_version);
    sink.set("config_hash", res.config_hash);
}

int run(int argc, char** argv) {
    const auto t0 = std::chrono::steady_clock::now();
    CliArgs cli = parse_cli(argc, argv);
    if (cli.help) { print_usage(std::cout); return 0; }
    if (cli.version) { std::cout << sim::SIM_VERSION << "\n"; return 0; }

    SIM_REQUIRE(!cli.config_path.empty(), "--config is required; try --help");
    SIM_REQUIRE(!cli.out_dir.empty(), "--out is required; try --help");

    sim::Config cfg;
    sim::json::Value root = sim::json::parse(read_file(cli.config_path), cli.config_path);
    std::set<std::string> provided = sim::config_load_json(cfg, root, cli.config_path);

    if (cli.has_seed) { cfg.seed = cli.seed; provided.insert("seed"); }
    if (!cli.positions.empty()) { cfg.positions = cli.positions; provided.insert("positions"); }
    if (cli.fast) { cfg.fast = true; provided.insert("fast"); }

    sim::config_validate(cfg, provided);
    sim::arm_lookup(cfg.arm);

    sim::Resolved res = sim::config_resolve(cfg);
    std::filesystem::create_directories(cli.out_dir);
    std::string out = cli.out_dir;
    if (!out.empty() && out.back() != '/' && out.back() != '\\') out += '/';
    write_file(out + "config.json", sim::config_echo(cfg, res));

    sim::RngService rng(static_cast<std::uint64_t>(cfg.seed));
    std::vector<sim::Node> nodes;
    sim::Topology topo = sim::build_topology(cfg, res, rng, nodes);
    sim::write_topology_csv(out + "topology.csv", topo);

    sim::MetricsSink sink;
    publish_config(cfg, res, sink);
    sink.set("E_0", topo.calib.E_0);
    sink.set("P_max", topo.calib.P_max);
    sink.set("K", static_cast<std::int64_t>(topo.K));
    sink.set("T_frame", static_cast<double>(topo.K) * cfg.T_slot);
    sink.set("tree_depth_max", static_cast<std::int64_t>(topo.depth_max));
    sink.set("deployment_rejections", topo.rejections);
    sink.set("mean_degree_gc", topo.mean_degree_gc);
    sink.set("max_degree_gi", static_cast<std::int64_t>(topo.max_degree_gi));

    std::unique_ptr<sim::Algorithm> arm = sim::make_arm(cfg.arm, cfg);
    sim::Network net(cfg, res, topo, nodes, *arm, out, sink);
    net.run();

    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sink.set("wall_clock_s", wall);
    sim::write_summary_csv(out + "summary.csv", sink);

    auto v = [&](const char* name) { return sink.text(name); };
    std::cout << "sim " << sim::SIM_VERSION << ": run " << res.run_id
              << " arm=" << cfg.arm << " rho=" << cfg.rho << " seed=" << cfg.seed
              << " hash=" << res.config_hash << " -> " << out << '\n'
              << "setup: profile=" << topo.profile << " positions=" << topo.positions_source
              << " rejections=" << topo.rejections << " K=" << topo.K
              << " depth_max=" << topo.depth_max << " mean_deg_gc=" << topo.mean_degree_gc
              << " max_deg_gi=" << topo.max_degree_gi
              << " P_max=" << topo.calib.P_max << " (node " << topo.calib.argmax << ")"
              << " E_0=" << topo.calib.E_0
              << (topo.profile == "paper" ? " I6=trivial(unique slots)" : " I6=ok") << '\n'
              << "run: frames=" << v("stop_frame") << " stop=" << v("stop_reason")
              << " fnd=" << v("fnd_frame") << " partition=" << v("partition_frame") << " adt=" << v("adt_frame")
              << " generated=" << v("generated_total") << " delivered=" << v("delivered_total")
              << " lost_orphan=" << v("lost_orphan_total") << " lost_death=" << v("lost_death_total")
              << " held=" << v("held_at_stop") << " pdr=" << v("pdr_global")
              << " energy=" << v("energy_total") << " i14_checked=" << v("i14_frames_checked")
              << " max_B=" << v("i16_max_B") << " trace=" << v("trace_stop_reason") << "/" << v("trace_frames_written")
              << " wall=" << wall << "s\n";
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const sim::InvariantFailure& e) {
        std::cerr << "sim: " << e.what() << "\n";
        return 3;
    } catch (const sim::json::ParseError& e) {
        std::cerr << "sim: config parse error: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "sim: error: " << e.what() << "\n";
        return 2;
    }
}
