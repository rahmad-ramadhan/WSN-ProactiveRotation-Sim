#include "io/nodes.hpp"

#include "core/assertions.hpp"
#include "core/format.hpp"

namespace sim {

NodesWriter::NodesWriter(const std::string& path) : out_(path, std::ios::binary), path_(path) {
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "id,dep,slot,death_frame,E_final,energy_data,energy_ctrl,parent_changes,"
            "frames_as_orphan,payloads_lost_as_orphan,final_parent,orphanings\n";
}

void NodesWriter::row(const Node& nd) {
    out_ << nd.id << ',' << nd.depth << ',' << nd.slot << ',' << nd.death_frame << ','
         << fmt::canonical(nd.E) << ',' << fmt::canonical(nd.energy_data) << ',' << fmt::canonical(nd.energy_ctrl) << ','
         << nd.parent_changes << ',' << nd.frames_as_orphan << ',' << nd.payloads_lost_as_orphan << ','
         << nd.parent << ',' << nd.orphanings << '\n';
}

void NodesWriter::close() {
    if (!out_.is_open()) return;
    out_.close();
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

}
