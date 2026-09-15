// Always compiled in, regardless of NDEBUG.
#pragma once

#include <sstream>
#include <stdexcept>
#include <string>

namespace sim {

struct InvariantFailure : std::runtime_error {
    std::string id;
    InvariantFailure(std::string invariant_id, const std::string& message)
        : std::runtime_error(message), id(std::move(invariant_id)) {}
};

struct RequirementFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] inline void invariant_failed(const char* id, const char* expr,
                                          const char* file, int line,
                                          const std::string& detail) {
    std::ostringstream os;
    os << "INVARIANT " << id << " FAILED: (" << expr << ") at " << file << ":" << line;
    if (!detail.empty()) os << " - " << detail;
    throw InvariantFailure(id, os.str());
}

[[noreturn]] inline void requirement_failed(const std::string& detail) {
    throw RequirementFailure(detail);
}

}

#define SIM_INVARIANT(id, cond, detail_stream)                                    \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::ostringstream sim_detail_os_;                                    \
            sim_detail_os_ << detail_stream;                                      \
            ::sim::invariant_failed(id, #cond, __FILE__, __LINE__,                \
                                    sim_detail_os_.str());                        \
        }                                                                         \
    } while (0)

#define SIM_REQUIRE(cond, detail_stream)                                          \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::ostringstream sim_detail_os_;                                    \
            sim_detail_os_ << detail_stream;                                      \
            ::sim::requirement_failed(sim_detail_os_.str());                      \
        }                                                                         \
    } while (0)

#define SIM_FAIL(detail_stream)                                                   \
    do {                                                                          \
        std::ostringstream sim_detail_os_;                                        \
        sim_detail_os_ << detail_stream;                                          \
        ::sim::requirement_failed(sim_detail_os_.str());                          \
    } while (0)
