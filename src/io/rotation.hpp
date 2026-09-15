// gaps.csv: f,id,incumbent,best,pri_incumbent,pri_best,gap,switched,inc_routed
// rotations.csv: f,id,old,new
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "core/node.hpp"

namespace sim {

class GapsWriter {
public:
    explicit GapsWriter(const std::string& path);
    void row(std::int64_t f, NodeId id, NodeId incumbent, NodeId best, double pri_incumbent, double pri_best,
             double gap, bool switched, bool inc_routed);
    void close();
private:
    std::ofstream out_;
    std::string path_;
};

class RotationsWriter {
public:
    explicit RotationsWriter(const std::string& path);
    void row(std::int64_t f, NodeId id, NodeId old_parent, NodeId new_parent);
    void close();
private:
    std::ofstream out_;
    std::string path_;
};

// recoveries.csv: f,id,event,old,new
// events: assign, backup, select, orphaned, request_sent, request_ok,
// request_failed, rejoin. Parent ids are -1 for none.
class RecoveriesWriter {
public:
    explicit RecoveriesWriter(const std::string& path);
    void row(std::int64_t f, NodeId id, const char* event, NodeId old_parent, NodeId new_parent);
    void close();
private:
    std::ofstream out_;
    std::string path_;
};

}
