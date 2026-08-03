#pragma once
#include <string>

// Built-in templates (c, cpp) are embedded into the dcap binary via #embed,
// so they work regardless of install location and with no env setup. A
// same-named entry in $DCAP_TEMPLATES overrides these (handled in scaffold).
namespace dcap {

bool is_builtin(const std::string& name); // true for "c" / "cpp"

// Write the built-in template <name> verbatim into <dest>. @NAME@ in the
// Makefile is patched by the caller. False on unknown name / write error.
bool write_builtin(const std::string& name, const std::string& dest);

// Per-language writers (defined in builtin_c.cpp / builtin_cpp.cpp).
bool write_builtin_c(const std::string& dest);
bool write_builtin_cpp(const std::string& dest);

} // namespace dcap
