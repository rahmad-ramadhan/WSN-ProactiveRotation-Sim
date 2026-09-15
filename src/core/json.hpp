// Minimal JSON reader. Objects keep key order.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sim::json {

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string raw;   // number source text, for exact integers
    std::string str;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    const Value* find(const std::string& key) const;

    const char* kind_name() const;
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Value parse(const std::string& text, const std::string& source);

}
