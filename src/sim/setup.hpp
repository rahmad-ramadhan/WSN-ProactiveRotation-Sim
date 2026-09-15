#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/node.hpp"
#include "core/rng.hpp"
#include "model/calibrate.hpp"
#include "model/geometry.hpp"
#include "model/graph.hpp"

namespace sim {

struct Topology {
    std::string profile;
    std::vector<geo::Point> pos;
    graph::Adjacency adj_gc;                // sink included
    graph::Adjacency adj_gi;                // sensors only
    graph::Tree tree;
    std::vector<std::int32_t> slot;
    std::vector<std::int64_t> desc;
    calib::Calibration calib;

    std::int32_t K = 0;                     // slots per frame
    std::int32_t depth_max = 0;
    std::int64_t rejections = 0;
    std::uint64_t rng_seed_used = 0;
    std::string positions_source;
    double mean_degree_gc = 0.0;
    std::int32_t max_degree_gi = 0;
    std::int64_t colouring_violations = 0;
    double tree_total_length = 0.0;

    std::uint64_t depth_checksum = 0;
    std::uint64_t slot_checksum = 0;
};

Topology build_topology(const Config& cfg, const Resolved& res, RngService& rng, std::vector<Node>& nodes);

std::uint64_t vector_checksum(const std::vector<std::int32_t>& v);

}
