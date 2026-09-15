// P_i   = E_tx(l*(1+D_i), d(i, par(i))) + E_rx(l*D_i)
// P_max = max over sensors of P_i
// E_0   = kappa * T_run * P_max
#pragma once

#include <cstdint>
#include <vector>

#include "model/energy.hpp"
#include "model/geometry.hpp"
#include "model/graph.hpp"

namespace sim::calib {

struct Calibration {
    std::vector<double> P;      // per id; P[0] = 0 for the sink
    double P_max = 0.0;
    std::int32_t argmax = 0;    // id of the hottest node
    double E_0 = 0.0;
};

Calibration calibrate(const std::vector<geo::Point>& pos, const graph::Tree& tree,
                      const std::vector<std::int64_t>& desc, const energy::Radio& radio,
                      double l_bits, double kappa, double T_run);

}
