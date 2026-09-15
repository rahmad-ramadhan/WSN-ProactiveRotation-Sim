#include "io/recovery_log.hpp"

#include "core/assertions.hpp"

namespace sim {

NemcrRecoveryWriter::NemcrRecoveryWriter(const std::string& path) : out_(path, std::ios::binary), path_(path) {
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "f,p,g,n_children,stage,c,target,attached,orphaned,inherited,new_violations\n";
}

void NemcrRecoveryWriter::row(std::int64_t f, NodeId p, NodeId g, std::int64_t n_children, const char* stage, NodeId c,
                              NodeId target, std::int64_t attached, std::int64_t orphaned, bool inherited,
                              std::int64_t new_violations) {
    out_ << f << ',' << p << ',' << g << ',' << n_children << ',' << stage << ',' << c << ',' << target << ','
         << attached << ',' << orphaned << ',' << (inherited ? 1 : 0) << ',' << new_violations << '\n';
}

void NemcrRecoveryWriter::close() {
    if (!out_.is_open()) return;
    out_.close();
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

NemcrLinksWriter::NemcrLinksWriter(const std::string& path) : out_(path, std::ios::binary), path_(path) {
    SIM_REQUIRE(out_.good(), "cannot open " << path << " for writing");
    out_ << "f,child,old,new\n";
}

void NemcrLinksWriter::row(std::int64_t f, NodeId child, NodeId old_parent, NodeId new_parent) {
    out_ << f << ',' << child << ',' << old_parent << ',' << new_parent << '\n';
}

void NemcrLinksWriter::close() {
    if (!out_.is_open()) return;
    out_.close();
    SIM_REQUIRE(out_.good(), "write failed: " << path_);
}

}
