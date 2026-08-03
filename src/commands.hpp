#pragma once
#include <string>

namespace dcap {

enum class Lang { Cpp, C };

// `dcap new <name>` / `dcap new-c <name>`: scaffold a project directory.
// Returns a process exit code (0 == success).
int cmd_new(const std::string& name, Lang lang);

// `dcap build`: run make in the current dir, then execute the built binary.
int cmd_build();

// Print CLI usage to stdout.
void print_usage();

} // namespace dcap
