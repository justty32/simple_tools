#pragma once
#include <string>

namespace dcap {

// dcap <template> <name>: create ./<name> from the resolved template.
// Returns a process exit code (0 == success).
int scaffold(const std::string& tmpl, const std::string& name);

// Print usage (to stderr), listing built-ins plus any DCAP_TEMPLATES entries.
void print_usage();

} // namespace dcap
