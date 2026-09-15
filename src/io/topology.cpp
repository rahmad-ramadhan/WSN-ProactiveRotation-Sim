#include "io/topology.hpp"

#include <fstream>

#include "core/assertions.hpp"
#include "core/format.hpp"

namespace sim {

void write_topology_csv(const std::string& path, const Topology& t) {
    std::ofstream out(path, std::ios::binary);
    SIM_REQUIRE(out.good(), "cannot open " << path << " for writing");
    out << "id,x,y,depth,slot,parent,deg_gc,deg_gi,desc,P\n";
    for (std::size_t i = 0; i < t.pos.size(); ++i) {
        out << i << ',' << fmt::canonical(t.pos[i].x) << ',' << fmt::canonical(t.pos[i].y) << ','
            << t.tree.depth[i] << ',' << t.slot[i] << ',' << t.tree.parent[i] << ','
            << t.adj_gc[i].size() << ',' << t.adj_gi[i].size() << ',' << t.desc[i] << ','
            << fmt::canonical(t.calib.P[i]) << '\n';
    }
    SIM_REQUIRE(out.good(), "write failed: " << path);
}

}
