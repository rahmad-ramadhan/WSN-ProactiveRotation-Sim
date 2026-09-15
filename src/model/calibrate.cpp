#include "model/calibrate.hpp"

#include "core/assertions.hpp"

namespace sim::calib {

Calibration calibrate(const std::vector<geo::Point>& pos, const graph::Tree& tree,
                      const std::vector<std::int64_t>& desc, const energy::Radio& radio,
                      double l_bits, double kappa, double T_run) {
    const auto n = pos.size();
    Calibration c;
    c.P.assign(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) {
        NodeId p = tree.parent[i];
        SIM_REQUIRE(p != NONE, "calibrate: node " << i << " has no parent on the initial tree");
        double d = geo::dist(pos[i], pos[static_cast<std::size_t>(p)]);
        auto D = static_cast<double>(desc[i]);
        c.P[i] = energy::E_tx(radio, l_bits * (1.0 + D), d) + energy::E_rx(radio, l_bits * D);
        if (c.P[i] > c.P_max) { c.P_max = c.P[i]; c.argmax = static_cast<std::int32_t>(i); }
    }
    c.E_0 = kappa * T_run * c.P_max;
    return c;
}

}
