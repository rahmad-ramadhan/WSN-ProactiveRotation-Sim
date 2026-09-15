#include "io/trace.hpp"

#include "core/assertions.hpp"
#include "core/format.hpp"

namespace sim {

FramesWriter::FramesWriter(const std::string& path) : out_(path, std::ios::binary), path_(path) {
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "f,alive,dead,orphan,routed,delivered,lost_orphan,lost_death,held_sum,"
            "energy_data,energy_ctrl,parent_changes,residual_min,residual_mean,residual_sd,max_B,orphanings\n";
}

void FramesWriter::write(const FrameRecord& r) {
    out_ << r.f << ',' << r.alive << ',' << r.dead << ',' << r.orphan << ',' << r.routed << ','
         << r.delivered << ',' << r.lost_orphan << ',' << r.lost_death << ',' << r.held_sum << ','
         << fmt::canonical(r.energy_data) << ',' << fmt::canonical(r.energy_ctrl) << ',' << r.parent_changes << ','
         << fmt::canonical(r.residual_min) << ',' << fmt::canonical(r.residual_mean) << ','
         << fmt::canonical(r.residual_sd) << ',' << r.max_B << ',' << r.orphanings << '\n';
}

void FramesWriter::close() {
    if (!out_.is_open()) return;
    out_.close();
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

TraceWriter::TraceWriter(const std::string& path, const TracePolicy& policy) : path_(path), policy_(policy) {
    if (policy_.frames_cap <= 0) { stopped_ = true; reason_ = "off"; return; }
    out_.open(path, std::ios::binary);
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "f,id,parent,sent,recv,B,E,charge,state\n";
    open_ = true;
}

bool TraceWriter::begin_frame(std::int64_t f, double routed_frac) {
    if (stopped_) return false;
    if (frames_written_ >= policy_.frames_cap) { stopped_ = true; reason_ = "cap"; return false; }
    if (routed_frac < policy_.min_routed) { stopped_ = true; reason_ = "connectivity"; return false; }
    if (policy_.every > 1 && f % policy_.every != 0) return false;
    ++frames_written_;
    return true;
}

void TraceWriter::row(std::int64_t f, const Node& nd, double charge) {
    const char* state = !nd.alive ? "dead" : (nd.route ? "ok" : "orphan");
    out_ << f << ',' << nd.id << ',' << nd.parent << ',' << nd.sent << ',' << nd.recv << ',' << nd.B << ','
         << fmt::canonical(nd.E) << ',' << fmt::canonical(charge) << ',' << state << '\n';
}

std::string TraceWriter::stop_reason() const { return stopped_ ? reason_ : "run_end"; }

void TraceWriter::close() {
    if (!open_) return;
    out_.close();
    open_ = false;
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

}
