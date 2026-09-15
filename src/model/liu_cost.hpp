// priced(e, theta) = e * exp(1 / sin(min(theta, pi - eps))), capped at `cap`.
#pragma once

#include <algorithm>
#include <cmath>

namespace liu {

constexpr double PI = 3.14159265358979323846;

struct Guards {
    double sin_eps;   // radians
    double cap;       // cap on a single priced cost
};

struct Priced {
    double c;
    bool capped;
};

inline Priced priced(double e, double theta, const Guards& g) {
    const double th = std::min(theta, PI - g.sin_eps);
    double c = e * std::exp(1.0 / std::sin(th));
    bool capped = false;
    if (!(c <= g.cap)) { c = g.cap; capped = true; }
    return {c, capped};
}

}
