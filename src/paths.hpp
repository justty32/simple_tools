#pragma once
#include <string>

namespace dcap {

// Value of $DCAP_TEMPLATES, or "" if unset/empty.
std::string external_root();

// Canonicalize a path spec (relative to cwd or absolute).
std::string canonical_dir(const std::string& spec);

} // namespace dcap
