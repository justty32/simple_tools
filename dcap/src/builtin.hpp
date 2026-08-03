#pragma once
#include <string>

// Built-in templates are embedded into the dcap binary via #embed, so they work
// regardless of install location and with no env setup. Which templates exist
// is decided at build time by scanning templates/ (tools/gen-embed.sh): every
// directory in there containing a Makefile becomes one. Nothing in the C++
// names a particular template. A same-named entry in $DCAP_TEMPLATES overrides
// a built-in (handled in scaffold).
namespace dcap {

bool is_builtin(const std::string& name);

// Built-in names for usage text, e.g. "c | cpp".
std::string builtin_names();

// Write the built-in template <name> into <dest>, creating whatever
// subdirectories its files need. @NAME@ in the Makefile is patched by the
// caller. False on unknown name / write error.
bool write_builtin(const std::string& name, const std::string& dest);

} // namespace dcap
