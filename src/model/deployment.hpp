#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/rng.hpp"
#include "model/geometry.hpp"

namespace sim::deploy {

struct Deployment {
    std::vector<geo::Point> pos;        // index = id; pos[0] is the sink
    std::int64_t rejections = 0;
    std::uint64_t rng_seed_used = 0;
    std::string source;
};

// Sink at (L/2, L/2). Disconnected draws are redrawn with the seed advanced by 1000.
Deployment draw_deployment(RngService& rng, std::int64_t rho, std::int64_t seed, std::int64_t N,
                           double L, double R_max);

// Rows id,x,y (header required, # comment lines allowed). A disconnected file is an error.
Deployment read_deployment(const std::string& path, std::int64_t N, double L, double R_max);

}
