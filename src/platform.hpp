#pragma once
#include <string>

// Platform helpers: keep every OS-specific branch behind these functions so
// the rest of dcap stays platform-neutral.
namespace dcap {

// ".exe" on Windows, "" elsewhere.
std::string exe_suffix();

// Shared library root. Uses env var DCAP_HOME if set, otherwise the per-OS
// default (~/dev/dcap on UNIX, C:/dev/dcap on Windows).
std::string dcap_home();

// The GNU make program to invoke: "mingw32-make" on Windows (falls back to
// "make"), "make" elsewhere (falls back to "mingw32-make").
std::string make_program();

// Thin std::system wrapper. Returns the raw system() status (0 == success).
int run(const std::string& cmd);

} // namespace dcap
