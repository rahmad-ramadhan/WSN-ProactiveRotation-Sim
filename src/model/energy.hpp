// settle() is the only place a node's E changes. The ledger also keeps a
// compensated total of every joule issued; I1 checks it against the node sums.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/node.hpp"

namespace sim::energy {

struct Radio {
    double E_elec;   // J/bit
    double eps_fs;   // J/bit/m^2
};

// No d^4 term: every link is within R_max < d_0.
inline double E_tx(const Radio& r, double bits, double d) {
    return bits * r.E_elec + bits * r.eps_fs * d * d;
}

inline double E_rx(const Radio& r, double bits) { return bits * r.E_elec; }

inline double E_tx_distance_part(const Radio& r, double bits, double d) { return bits * r.eps_fs * d * d; }

struct ControlCostModel {
    std::string name;
    bool charges;
};

enum class Kind { DATA_TX, DATA_RX, CTRL_TX, CTRL_RX };
const char* kind_name(Kind k);

struct Entry {
    double bits;
    Kind kind;
    double d;        // metres for TX; unused for RX
};

// Neumaier compensated summation.
class CompensatedSum {
public:
    void add(double x);
    double value() const { return sum_ + comp_; }
private:
    double sum_ = 0.0, comp_ = 0.0;
};

struct Settlement {
    double charge = 0.0;
    double data = 0.0;        // DATA_TX + DATA_RX
    double ctrl = 0.0;        // CTRL_TX + CTRL_RX, zero under `free`
    double distance = 0.0;
    std::int64_t rx_bits = 0;
};

class Ledger {
public:
    Ledger(const Radio& radio, const ControlCostModel& ctrl_model, std::size_t n_ids, std::int64_t l_data);

    void data_tx(NodeId u, double bits, double d);
    void data_rx(NodeId p, double bits);
    void ctrl_tx(NodeId u, double bits, double d);
    void ctrl_rx(NodeId v, double bits);

    std::size_t size(NodeId i) const { return entries_[static_cast<std::size_t>(i)].size(); }
    const std::vector<Entry>& entries(NodeId i) const { return entries_[static_cast<std::size_t>(i)]; }

    Settlement settle(Node& nd, double alpha);

    double total_issued() const { return total_.value(); }
    double total_distance_component() const { return distance_.value(); }
    double total_control_energy() const { return ctrl_energy_.value(); }
    std::int64_t data_bits_total() const { return data_bits_; }
    std::int64_t control_bits_total() const { return ctrl_bits_; }

private:
    Radio radio_;
    ControlCostModel ctrl_;
    std::int64_t l_;
    std::vector<std::vector<Entry>> entries_;
    CompensatedSum total_, distance_, ctrl_energy_;
    std::int64_t data_bits_ = 0, ctrl_bits_ = 0;

    void append(NodeId i, Entry e);
};

}
