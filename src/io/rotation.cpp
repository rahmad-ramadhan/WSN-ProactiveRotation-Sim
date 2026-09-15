#include "io/rotation.hpp"

#include "core/assertions.hpp"
#include "core/format.hpp"

namespace sim {

GapsWriter::GapsWriter(const std::string& path) : out_(path, std::ios::binary), path_(path) {
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "f,id,incumbent,best,pri_incumbent,pri_best,gap,switched,inc_routed\n";
}

void GapsWriter::row(std::int64_t f, NodeId id, NodeId incumbent, NodeId best, double pri_incumbent, double pri_best,
                     double gap, bool switched, bool inc_routed) {
    out_ << f << ',' << id << ',' << incumbent << ',' << best << ',' << fmt::canonical(pri_incumbent) << ','
         << fmt::canonical(pri_best) << ',' << fmt::canonical(gap) << ',' << (switched ? 1 : 0) << ','
         << (inc_routed ? 1 : 0) << '\n';
}

void GapsWriter::close() {
    if (!out_.is_open()) return;
    out_.close();
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

RotationsWriter::RotationsWriter(const std::string& path) : out_(path, std::ios::binary), path_(path) {
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "f,id,old,new\n";
}

void RotationsWriter::row(std::int64_t f, NodeId id, NodeId old_parent, NodeId new_parent) {
    out_ << f << ',' << id << ',' << old_parent << ',' << new_parent << '\n';
}

void RotationsWriter::close() {
    if (!out_.is_open()) return;
    out_.close();
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

RecoveriesWriter::RecoveriesWriter(const std::string& path) : out_(path, std::ios::binary), path_(path) {
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "f,id,event,old,new\n";
}

void RecoveriesWriter::row(std::int64_t f, NodeId id, const char* event, NodeId old_parent, NodeId new_parent) {
    out_ << f << ',' << id << ',' << event << ',' << old_parent << ',' << new_parent << '\n';
}

void RecoveriesWriter::close() {
    if (!out_.is_open()) return;
    out_.close();
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

}
