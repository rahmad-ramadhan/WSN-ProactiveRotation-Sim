#pragma once

#include <memory>
#include <string>
#include <vector>

#include "arms/algorithm.hpp"

namespace sim {

struct Config;

using ArmFactory = std::unique_ptr<Algorithm> (*)(const Config&);

struct ArmEntry {
    std::string name;
    std::string summary;
    ArmFactory factory;
};

const std::vector<ArmEntry>& arm_table();
std::vector<std::string> arm_names();
std::string arm_names_text();
const ArmEntry& arm_lookup(const std::string& name);   // throws if unknown
std::unique_ptr<Algorithm> make_arm(const std::string& name, const Config& cfg);

}
