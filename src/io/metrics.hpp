// Summary columns are declared once in metrics.cpp. Header and row are generated
// from that list; setting an unknown column is an error.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace sim {

// Frame: a frame index, or `not reached` if the event did not happen before the stop.
enum class ColType { Int, Real, Str, Frame };

inline constexpr const char* NOT_REACHED = "not reached";

using MetricValue = std::variant<std::int64_t, double, std::string>;

struct ColumnDef {
    std::string name;
    ColType type;
    std::string source;
    // Written when the owning arm is not the one running.
    std::optional<MetricValue> dflt;
};

const std::vector<ColumnDef>& summary_columns();
const ColumnDef* find_column(const std::string& name);

class MetricsSink {
public:
    using Value = MetricValue;

    void set(const std::string& name, std::int64_t v);
    void set(const std::string& name, double v);
    void set(const std::string& name, const std::string& v);
    void set(const std::string& name, const char* v) { set(name, std::string(v)); }
    void set_frame(const std::string& name, std::optional<std::int64_t> f);

    bool has(const std::string& name) const { return values_.contains(name); }

    std::string text(const std::string& name) const;

private:
    std::map<std::string, Value> values_;
    void check(const std::string& name, ColType t) const;
};

std::string summary_header();
std::string summary_row(const MetricsSink& sink);

void write_summary_csv(const std::string& path, const MetricsSink& sink);

}
