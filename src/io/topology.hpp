#pragma once

#include <string>

#include "sim/setup.hpp"

namespace sim {

// Columns: id,x,y,depth,slot,parent,deg_gc,deg_gi,desc,P
void write_topology_csv(const std::string& path, const Topology& t);

}
