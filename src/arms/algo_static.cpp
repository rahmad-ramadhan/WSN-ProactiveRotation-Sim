// static: never re-routes. The frame loop orphans the children of a dead parent.
#include <memory>

#include "arms/algorithm.hpp"
#include "core/config.hpp"

namespace sim {

namespace {
class StaticArm final : public Algorithm {};
}

std::unique_ptr<Algorithm> make_static_arm(const Config&) { return std::make_unique<StaticArm>(); }

}
