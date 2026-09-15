// dcfr_decisions.csv: f,stage,id,old,new,total,n_cand,rule1_active,rule1_binding
// new = -1 and total = inf when there was no candidate.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "core/node.hpp"

namespace sim {

class DcfrDecisionsWriter {
public:
    explicit DcfrDecisionsWriter(const std::string& path);
    void row(std::int64_t f, const char* stage, NodeId id, NodeId old_parent, NodeId new_parent, double total,
             std::int64_t n_cand, bool rule1_active, bool rule1_binding);
    void close();
private:
    std::ofstream out_;
    std::string path_;
};

}
