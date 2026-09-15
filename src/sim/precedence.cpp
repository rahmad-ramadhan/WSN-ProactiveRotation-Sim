#include "sim/precedence.hpp"

#include "core/assertions.hpp"

namespace sim {

namespace {
struct Rule {
    std::string name;
    PrecedesFn fn;
};
const std::vector<Rule>& rules() {
    static const std::vector<Rule> table = {
        {"depth", &precedes_depth},
        {"path_vector", nullptr},
        {"sink_distance", nullptr},
    };
    return table;
}
}

PrecedesFn precedence_rule(const std::string& name) {
    for (const auto& r : rules()) {
        if (r.name != name) continue;
        SIM_REQUIRE(r.fn != nullptr, "precedence_rule \"" << name
                                     << "\" is registered but not built yet; only \"depth\" exists (BUILD_PLAN.md ordering note)");
        return r.fn;
    }
    std::string valid;
    for (const auto& r : rules()) { if (!valid.empty()) valid += ", "; valid += r.name; }
    SIM_FAIL("unknown precedence_rule \"" << name << "\"; valid: " << valid);
}

}
