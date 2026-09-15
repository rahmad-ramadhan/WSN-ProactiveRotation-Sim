// Every hook defaults to a no-op.
#pragma once

#include "core/node.hpp"

namespace sim {

class AlgorithmContext;

struct InvariantPolicy {
    bool i5_counted = false;   // static-order violations counted, not fatal
    bool i6_counted = false;   // slot colouring violations counted, not fatal
};

class Algorithm {
public:
    virtual ~Algorithm() = default;

    virtual void init(AlgorithmContext&) {}                     // once, after setup
    virtual void control(AlgorithmContext&) {}                  // S3, the only stage that may send
    virtual void react(AlgorithmContext&, NodeId /*dead_parent*/) {}  // S5, once per dead parent
    virtual void orphan_rule(AlgorithmContext&, NodeId /*node*/) {}   // S5, parent alive but route lost
    virtual void rotate(AlgorithmContext&) {}                   // S6
    virtual void finish(AlgorithmContext&) {}                   // once, at the end

    virtual InvariantPolicy invariant_policy() const { return {}; }
};

}
