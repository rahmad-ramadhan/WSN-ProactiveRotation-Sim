#include "io/dcfr_log.hpp"

#include "core/assertions.hpp"
#include "core/format.hpp"

namespace sim {

DcfrDecisionsWriter::DcfrDecisionsWriter(const std::string& path) : out_(path, std::ios::binary), path_(path) {
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "f,stage,id,old,new,total,n_cand,rule1_active,rule1_binding\n";
}

void DcfrDecisionsWriter::row(std::int64_t f, const char* stage, NodeId id, NodeId old_parent, NodeId new_parent,
                              double total, std::int64_t n_cand, bool rule1_active, bool rule1_binding) {
    out_ << f << ',' << stage << ',' << id << ',' << old_parent << ',' << new_parent << ',' << fmt::canonical(total)
         << ',' << n_cand << ',' << (rule1_active ? 1 : 0) << ',' << (rule1_binding ? 1 : 0) << '\n';
}

void DcfrDecisionsWriter::close() {
    if (!out_.is_open()) return;
    out_.close();
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

}
