#include "model/energy.hpp"

#include <algorithm>
#include <cmath>

#include "core/assertions.hpp"

namespace sim::energy {

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::DATA_TX: return "DATA_TX";
        case Kind::DATA_RX: return "DATA_RX";
        case Kind::CTRL_TX: return "CTRL_TX";
        case Kind::CTRL_RX: return "CTRL_RX";
    }
    return "?";
}

void CompensatedSum::add(double x) {
    double t = sum_ + x;
    if (std::fabs(sum_) >= std::fabs(x)) comp_ += (sum_ - t) + x;
    else comp_ += (x - t) + sum_;
    sum_ = t;
}

Ledger::Ledger(const Radio& radio, const ControlCostModel& ctrl_model, std::size_t n_ids, std::int64_t l_data)
    : radio_(radio), ctrl_(ctrl_model), l_(l_data), entries_(n_ids) {}

void Ledger::append(NodeId i, Entry e) {
    SIM_INVARIANT("I15", i != SINK, "attempt to append " << kind_name(e.kind) << " of " << e.bits << " bits to the sink's ledger");
    SIM_REQUIRE(i > SINK && static_cast<std::size_t>(i) < entries_.size(), "ledger: bad node id " << i);
    entries_[static_cast<std::size_t>(i)].push_back(e);
}

void Ledger::data_tx(NodeId u, double bits, double d) {
    append(u, {bits, Kind::DATA_TX, d});
    data_bits_ += static_cast<std::int64_t>(bits);
}

void Ledger::data_rx(NodeId p, double bits) {
    SIM_INVARIANT("I11", std::fmod(bits, static_cast<double>(l_)) == 0.0,
                  "node " << p << " receives " << bits << " bits, not a multiple of l=" << l_);
    append(p, {bits, Kind::DATA_RX, 0.0});
}

void Ledger::ctrl_tx(NodeId u, double bits, double d) {
    append(u, {bits, Kind::CTRL_TX, d});
    ctrl_bits_ += static_cast<std::int64_t>(bits);
}

void Ledger::ctrl_rx(NodeId v, double bits) { append(v, {bits, Kind::CTRL_RX, 0.0}); }

Settlement Ledger::settle(Node& nd, double alpha) {
    Settlement s;
    auto& led = entries_[static_cast<std::size_t>(nd.id)];
    // CTRL entries are skipped under `free`.
    for (const Entry& e : led) {
        switch (e.kind) {
            case Kind::DATA_TX: {
                double c = E_tx(radio_, e.bits, e.d);
                s.charge += c; s.data += c;
                s.distance += E_tx_distance_part(radio_, e.bits, e.d);
                break;
            }
            case Kind::DATA_RX: {
                double c = E_rx(radio_, e.bits);
                s.charge += c; s.data += c;
                s.rx_bits += static_cast<std::int64_t>(e.bits);
                break;
            }
            case Kind::CTRL_TX:
                if (ctrl_.charges) {
                    double c = E_tx(radio_, e.bits, e.d);
                    s.charge += c; s.ctrl += c;
                    s.distance += E_tx_distance_part(radio_, e.bits, e.d);
                }
                break;
            case Kind::CTRL_RX:
                if (ctrl_.charges) {
                    double c = E_rx(radio_, e.bits);
                    s.charge += c; s.ctrl += c;
                }
                break;
        }
    }
    nd.E -= s.charge;
    nd.E_cum += s.charge;
    nd.energy_data += s.data;
    nd.energy_ctrl += s.ctrl;
    nd.CES = s.charge;
    nd.MES = std::max(nd.MES, nd.CES);
    nd.P_hat = alpha * s.charge + (1.0 - alpha) * nd.P_hat;
    led.clear();
    total_.add(s.charge);
    distance_.add(s.distance);
    ctrl_energy_.add(s.ctrl);
    return s;
}

}
