#pragma once

#include <cmath>

namespace sim::geo {

struct Point {
    double x = 0.0, y = 0.0;
};

inline double dist2(const Point& a, const Point& b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline double dist(const Point& a, const Point& b) { return std::sqrt(dist2(a, b)); }

// Squared distances, so graph builds take no sqrt.
inline bool within(const Point& a, const Point& b, double r) { return dist2(a, b) <= r * r; }

}
