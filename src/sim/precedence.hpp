// precedes(j, i) iff (dep(j), id_j) < (dep(i), id_i). The sink precedes everything.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/node.hpp"

namespace sim {

using PrecedesFn = bool (*)(const std::vector<std::int32_t>& depth, NodeId j, NodeId i);

inline bool precedes_depth(const std::vector<std::int32_t>& depth, NodeId j, NodeId i) {
    auto dj = depth[static_cast<std::size_t>(j)], di = depth[static_cast<std::size_t>(i)];
    if (dj != di) return dj < di;
    return j < i;
}

PrecedesFn precedence_rule(const std::string& name);
}
