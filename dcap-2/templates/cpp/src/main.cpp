#include <cstdlib>
#include <iostream>
#include "lib.hpp"

// This binary is a shared object that the kernel can also execute directly.
// PT_INTERP names the dynamic loader; DCAP_INTERP comes from CMakeLists.txt.
extern "C" __attribute__((used, section(".interp"))) const char dcap_interp[] =
    DCAP_INTERP;

// Entry point for `./main`, selected with -Wl,-e. Three things differ from a
// normal main():
//   * No crt0 runs, so this must never return — call exit() instead.
//   * argc/argv are not passed in (read /proc/self/cmdline if you need them).
//   * The ELF entry point receives a 16-byte-aligned RSP, but a function
//     prologue expects the post-call layout (RSP % 16 == 8), so it has to
//     realign — otherwise any aligned SSE spill segfaults.
extern "C" __attribute__((force_align_arg_pointer)) [[noreturn]] void
dcap_main() {
    std::cout << "2 + 3 = " << lib::add(2, 3) << "\n";
    std::exit(0);
}
