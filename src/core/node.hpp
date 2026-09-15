#pragma once

#include <cstdint>
#include <vector>

namespace sim {

using NodeId = std::int32_t;
inline constexpr NodeId SINK = 0;
inline constexpr NodeId NONE = -1;

// Contents of a control broadcast, as of the end of frame f-1.
struct ViewRecord {
    double E = 0.0;
    double P_hat = 0.0;
    double Delta = 0.0;
    double MTC = 0.0;
    double CES = 0.0;
    double MES = 0.0;
    double PM2 = 0.0;
    bool route = false;
    std::int64_t frame = 0;
};

struct View {
    bool has = false;
    ViewRecord rec;
};

struct Node {
    // set at setup
    NodeId id = NONE;
    double x = 0.0, y = 0.0;
    std::int32_t slot = 0;              // only nemcr slot inheritance changes it later
    std::int32_t depth = 0;             // frozen after setup
    std::vector<NodeId> nbr;            // within R_max, sink included

    // written in S4
    bool alive = true;
    double E = 0.0;                     // signed, never clamped
    double E_cum = 0.0;
    double CES = 0.0;                   // consumption in the last closed frame
    double MES = 0.0;                   // running max of CES
    double P_hat = 0.0;                 // EWMA of per-frame consumption
    std::int64_t death_frame = 0;       // 0 = alive
    double energy_data = 0.0;
    double energy_ctrl = 0.0;

    // written in S2, cleared on death
    std::int64_t B = 0;
    std::int64_t sent = 0;
    std::int64_t recv = 0;              // radpr reads it one frame late
    std::int64_t payloads_lost_as_orphan = 0;

    // set in S1, cleared in S2
    std::int32_t pending = 0;

    // written through AlgorithmContext::set_parent in S5 / S6
    NodeId parent = NONE;
    NodeId last_parent = NONE;          // most recent non-NONE parent
    double MTC = 0.0;                   // advertised path cost
    std::int64_t parent_changes = 0;    // switches to a real parent
    std::int64_t orphanings = 0;        // writes to NONE

    // recomputed at the end of S5
    bool route = false;
    std::int64_t frames_as_orphan = 0;

    // written in S3; view[k] is from nbr[k]
    std::vector<View> view;

    // set in S5, cleared in S3
    bool ctrl_due = false;
};

}
