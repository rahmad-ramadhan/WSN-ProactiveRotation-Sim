// nodes.csv: one row per sensor, written at death, then survivors at stop.
#pragma once

#include <fstream>
#include <string>

#include "core/node.hpp"

namespace sim {

class NodesWriter {
public:
    explicit NodesWriter(const std::string& path);
    void row(const Node& nd);
    void close();
private:
    std::ofstream out_;
    std::string path_;
};

}
