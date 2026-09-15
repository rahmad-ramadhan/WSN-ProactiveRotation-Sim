// nemcr_recovery.csv: f,p,g,n_children,stage,c,target,attached,orphaned,inherited,new_violations
// stage: A, B, none, stale, unplanned, or - when p had no alive children.
//
// nemcr_links.csv: f,child,old,new (new = -1 when orphaned).
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "core/node.hpp"

namespace sim {

class NemcrRecoveryWriter {
public:
    explicit NemcrRecoveryWriter(const std::string& path);
    void row(std::int64_t f, NodeId p, NodeId g, std::int64_t n_children, const char* stage, NodeId c, NodeId target,
             std::int64_t attached, std::int64_t orphaned, bool inherited, std::int64_t new_violations);
    void close();
private:
    std::ofstream out_;
    std::string path_;
};

class NemcrLinksWriter {
public:
    explicit NemcrLinksWriter(const std::string& path);
    void row(std::int64_t f, NodeId child, NodeId old_parent, NodeId new_parent);
    void close();
private:
    std::ofstream out_;
    std::string path_;
};

}
