#include "core/rng.hpp"

#include "core/assertions.hpp"
#include "core/format.hpp"

namespace sim {

namespace {
// splitmix64 finaliser
std::uint64_t mix(std::uint64_t z) {
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
}

Rng& RngService::open(const std::string& name, std::uint64_t seed) {
    SIM_REQUIRE(!streams_.contains(name), "rng stream \"" << name << "\" opened twice");
    auto [it, _] = streams_.emplace(name, Rng(seed));
    order_.push_back(name);
    return it->second;
}

Rng& RngService::open_derived(const std::string& name) {
    return open(name, mix(master_ ^ fmt::fnv1a64(name)));
}

Rng& RngService::stream(const std::string& name) {
    auto it = streams_.find(name);
    SIM_REQUIRE(it != streams_.end(), "rng stream \"" << name << "\" is not open");
    return it->second;
}

}
