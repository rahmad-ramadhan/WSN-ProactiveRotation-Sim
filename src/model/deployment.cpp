#include "model/deployment.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/assertions.hpp"
#include "model/graph.hpp"

namespace sim::deploy {

namespace {

std::vector<geo::Point> draw_once(Rng& stream, std::int64_t N, double L) {
    std::vector<geo::Point> pos(static_cast<std::size_t>(N) + 1);
    pos[0] = {L / 2.0, L / 2.0};
    // x = L*u_{2i-1}, y = L*u_{2i}
    for (std::int64_t i = 1; i <= N; ++i) {
        double ux = stream.next_unit();
        double uy = stream.next_unit();
        pos[static_cast<std::size_t>(i)] = {L * ux, L * uy};
    }
    return pos;
}

bool connected(const std::vector<geo::Point>& pos, double R_max) {
    return graph::connected_to_sink(graph::build_adjacency(pos, R_max, true));
}

std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

double parse_double(const std::string& s, const std::string& where) {
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    SIM_REQUIRE(errno == 0 && end && *end == '\0' && !s.empty() && std::isfinite(v),
                where << ": bad number \"" << s << "\"");
    return v;
}

}

Deployment draw_deployment(RngService& rng, std::int64_t rho, std::int64_t seed, std::int64_t N,
                           double L, double R_max) {
    Deployment d;
    d.source = "rng";
    for (std::int64_t attempt = 0;; ++attempt) {
        // stride 1000 so retry seeds never collide
        auto mt_seed = static_cast<std::uint64_t>(rho * 1000 + seed + 1000 * attempt);
        std::string name = attempt == 0 ? "positions" : "positions." + std::to_string(attempt);
        Rng& stream = rng.open(name, mt_seed);
        d.pos = draw_once(stream, N, L);
        if (connected(d.pos, R_max)) {
            d.rejections = attempt;
            d.rng_seed_used = mt_seed;
            return d;
        }
        SIM_REQUIRE(attempt < 100000, "deployment rejected 100000 times at rho=" << rho << " seed=" << seed
                                      << "; the field cannot be connected at this density");
    }
}

Deployment read_deployment(const std::string& path, std::int64_t N, double L, double R_max) {
    std::ifstream in(path, std::ios::binary);
    SIM_REQUIRE(in.good(), "cannot read positions file " << path);
    Deployment d;
    d.source = path;
    d.pos.assign(static_cast<std::size_t>(N) + 1, {});
    std::vector<char> seen(static_cast<std::size_t>(N) + 1, 0);

    std::string line;
    bool header_seen = false;
    std::int64_t rows = 0, lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (!header_seen) {
            SIM_REQUIRE(t == "id,x,y", path << ":" << lineno << ": expected header \"id,x,y\", got \"" << t << "\"");
            header_seen = true;
            continue;
        }
        std::string where = path + ":" + std::to_string(lineno);
        std::stringstream ss(t);
        std::string f_id, f_x, f_y, extra;
        SIM_REQUIRE(std::getline(ss, f_id, ',') && std::getline(ss, f_x, ',') && std::getline(ss, f_y, ','),
                    where << ": expected three fields id,x,y");
        SIM_REQUIRE(!std::getline(ss, extra, ','), where << ": more than three fields");
        f_id = trim(f_id);
        SIM_REQUIRE(!f_id.empty() && f_id.find_first_not_of("0123456789") == std::string::npos,
                    where << ": bad id \"" << f_id << "\"");
        std::int64_t id = std::strtoll(f_id.c_str(), nullptr, 10);
        SIM_REQUIRE(id == rows, where << ": ids must be ascending from 0 with no gaps; expected " << rows << ", got " << id);
        SIM_REQUIRE(id <= N, where << ": id " << id << " exceeds N=" << N
                                   << " (the file has more rows than the configured N)");
        double x = parse_double(trim(f_x), where), y = parse_double(trim(f_y), where);
        SIM_REQUIRE(x >= 0.0 && x <= L && y >= 0.0 && y <= L,
                    where << ": position (" << x << ", " << y << ") outside [0, L] with L=" << L
                          << " (L follows from rho and N; check rho)");
        d.pos[static_cast<std::size_t>(id)] = {x, y};
        seen[static_cast<std::size_t>(id)] = 1;
        ++rows;
    }
    SIM_REQUIRE(header_seen, path << ": empty positions file");
    SIM_REQUIRE(rows == N + 1, path << ": expected " << (N + 1) << " rows (sink + N=" << N << " nodes), got " << rows);
    double cx = d.pos[0].x, cy = d.pos[0].y;
    SIM_REQUIRE(std::fabs(cx - L / 2.0) <= 1e-6 && std::fabs(cy - L / 2.0) <= 1e-6,
                path << ": sink (id 0) must sit at (L/2, L/2) = (" << L / 2.0 << ", " << L / 2.0
                     << "), got (" << cx << ", " << cy << ")");
    SIM_REQUIRE(connected(d.pos, R_max),
                path << ": deployment is not connected to the sink; a positions file must already be "
                        "connected (the generator applies the rejection)");
    return d;
}

}
