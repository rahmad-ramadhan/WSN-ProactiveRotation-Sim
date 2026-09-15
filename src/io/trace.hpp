#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "core/node.hpp"

namespace sim {

struct FrameRecord {
    std::int64_t f = 0;
    std::int64_t alive = 0, dead = 0, orphan = 0, routed = 0;
    std::int64_t delivered = 0, lost_orphan = 0, lost_death = 0, held_sum = 0;
    double energy_data = 0.0, energy_ctrl = 0.0;
    std::int64_t parent_changes = 0;
    double residual_min = 0.0, residual_mean = 0.0, residual_sd = 0.0;
    std::int64_t max_B = 0;
    std::int64_t orphanings = 0;
};

class FramesWriter {
public:
    explicit FramesWriter(const std::string& path);
    void write(const FrameRecord& r);
    void close();
private:
    std::ofstream out_;
    std::string path_;
};

struct TracePolicy {
    std::int64_t frames_cap = 0;     // 0 = off
    std::int64_t every = 1;          // write when f % every == 0
    double min_routed = 0.0;         // stop once routed/N falls below this
};

// trace.csv: f,id,parent,sent,recv,B,E,charge,state
// state is ok, orphan or dead. Every traced frame has N rows.
class TraceWriter {
public:
    TraceWriter(const std::string& path, const TracePolicy& policy);

    bool begin_frame(std::int64_t f, double routed_frac);
    void row(std::int64_t f, const Node& nd, double charge);

    // "off", "cap", "connectivity" or "run_end"
    std::string stop_reason() const;
    std::int64_t frames_written() const { return frames_written_; }
    void close();

private:
    std::ofstream out_;
    std::string path_;
    TracePolicy policy_;
    bool open_ = false;
    bool stopped_ = false;
    std::string reason_;
    std::int64_t frames_written_ = 0;
};

}
