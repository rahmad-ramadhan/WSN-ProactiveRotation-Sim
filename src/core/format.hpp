// Doubles print as the shortest text that round-trips, with ".0" added when
// the result looks like an integer, so equal values give identical bytes.
#pragma once

#include <charconv>
#include <cstdio>
#include <cstdint>
#include <string>

namespace sim::fmt {

inline std::string canonical(double x) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), x);
    std::string s(buf, res.ptr);
    bool looks_integral = true;
    for (char c : s)
        if (c == '.' || c == 'e' || c == 'E' || c == 'n' || c == 'i') { looks_integral = false; break; }
    if (looks_integral) s += ".0";
    return s;
}

inline std::string canonical(std::int64_t x) { return std::to_string(x); }

inline std::string canonical(bool b) { return b ? "true" : "false"; }

inline std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

inline std::string csv_field(const std::string& s) {
    bool needs = false;
    for (char c : s)
        if (c == ',' || c == '"' || c == '\n' || c == '\r') { needs = true; break; }
    if (!needs) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    out += '"';
    return out;
}

inline std::uint64_t fnv1a64(const std::string& s, std::uint64_t h = 0xcbf29ce484222325ULL) {
    for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ULL; }
    return h;
}

inline std::string hex64(std::uint64_t v) {
    static const char* digits = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) { s[static_cast<std::size_t>(i)] = digits[v & 0xF]; v >>= 4; }
    return s;
}

}
