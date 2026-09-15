// Named RNG streams. Positions are the only random draw, seeded rho * 1000 + seed.
#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace sim {

class Rng {
public:
    explicit Rng(std::uint64_t seed) : engine_(seed), seed_(seed) {}

    std::uint64_t seed() const { return seed_; }
    std::uint64_t draws() const { return draws_; }

    std::uint64_t next_u64() { ++draws_; return engine_(); }

    // top 53 bits of one output over 2^53
    double next_unit() {
        return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
    }

private:
    std::mt19937_64 engine_;
    std::uint64_t seed_;
    std::uint64_t draws_ = 0;
};

class RngService {
public:
    explicit RngService(std::uint64_t master_seed) : master_(master_seed) {}

    std::uint64_t master_seed() const { return master_; }

    Rng& open(const std::string& name, std::uint64_t seed);

    Rng& open_derived(const std::string& name);

    Rng& stream(const std::string& name);

    const std::vector<std::string>& names() const { return order_; }

private:
    std::uint64_t master_;
    std::map<std::string, Rng> streams_;
    std::vector<std::string> order_;
};
}
