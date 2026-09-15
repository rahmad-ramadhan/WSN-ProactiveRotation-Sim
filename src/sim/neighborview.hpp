// explicit: the last record received from j.
// oracle: j's true state, copied at the start of S5 / S6.
#pragma once

#include <string>
#include <vector>

#include "core/node.hpp"
#include "sim/context.hpp"

namespace sim {

struct KnowledgeMode {
    std::string name;
    bool oracle;
};

class NeighborView {
public:
    NeighborView(const KnowledgeMode& mode, const AlgorithmContext& ctx) : oracle_(mode.oracle), ctx_(ctx) {}

    template <class F>
    void begin_stage(F current) {
        if (!oracle_) return;
        snapshot_.assign(ctx_.nodes.size(), View{});
        for (std::size_t j = 1; j < ctx_.nodes.size(); ++j)
            if (ctx_.nodes[j].alive) snapshot_[j] = View{true, current(ctx_.nodes[j].id)};
    }

    bool lookup(NodeId i, NodeId j, ViewRecord& out) const {
        if (oracle_) {
            const View& v = snapshot_[static_cast<std::size_t>(j)];
            if (!v.has) return false;
            out = v.rec;
            return true;
        }
        const std::int32_t idx = ctx_.view_index(i, j);
        if (idx < 0) return false;
        const View& v = ctx_.nodes[static_cast<std::size_t>(i)].view[static_cast<std::size_t>(idx)];
        if (!v.has) return false;
        out = v.rec;
        return true;
    }

    bool oracle() const { return oracle_; }

private:
    bool oracle_;
    const AlgorithmContext& ctx_;
    std::vector<View> snapshot_;
};

}
