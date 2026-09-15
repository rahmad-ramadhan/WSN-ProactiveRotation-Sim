#include "arms/registry.hpp"

#include "core/assertions.hpp"

namespace sim {

std::unique_ptr<Algorithm> make_static_arm(const Config&);
std::unique_ptr<Algorithm> make_radpr_arm(const Config&);
std::unique_ptr<Algorithm> make_nemcr_arm(const Config&);
std::unique_ptr<Algorithm> make_dcfr_arm(const Config&);
std::unique_ptr<Algorithm> make_escfr_arm(const Config&);
std::unique_ptr<Algorithm> make_fa_arm(const Config&);

const std::vector<ArmEntry>& arm_table() {
    static const std::vector<ArmEntry> table = {
        {"static", "no control, no re-evaluation, orphans stay orphaned", &make_static_arm},
        {"radpr",  "RA-DPR proactive rotation: PRI score, rotation, recovery", &make_radpr_arm},
        {"nemcr",  "Urmonov & Kim 2018 backup-parent recovery with slot inheritance", &make_nemcr_arm},
        {"dcfr",   "Liu et al. 2012 energy + rate cost-function routing", &make_dcfr_arm},
        {"escfr",  "Liu et al. 2012 energy-only cost-function routing", &make_escfr_arm},
        {"fa",     "Chang & Tassiulas 2000 flow-augmentation power-law cost, summed", &make_fa_arm},
    };
    return table;
}

std::vector<std::string> arm_names() {
    std::vector<std::string> v;
    for (const auto& e : arm_table()) v.push_back(e.name);
    return v;
}

std::string arm_names_text() {
    std::string s;
    for (const auto& e : arm_table()) { if (!s.empty()) s += ", "; s += e.name; }
    return s;
}

const ArmEntry& arm_lookup(const std::string& name) {
    for (const auto& e : arm_table())
        if (e.name == name) return e;
    SIM_FAIL("unknown arm \"" << name << "\"; valid arms: " << arm_names_text());
}

std::unique_ptr<Algorithm> make_arm(const std::string& name, const Config& cfg) {
    const ArmEntry& e = arm_lookup(name);
    SIM_REQUIRE(e.factory != nullptr,
                "arm \"" << name << "\" is registered but not implemented");
    return e.factory(cfg);
}

}
