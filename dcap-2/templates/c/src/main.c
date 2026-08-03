#include <stdio.h>
#include <stdlib.h>
#include "lib.h"

/* This binary is a shared object that the kernel can also execute directly.
   PT_INTERP names the dynamic loader; DCAP_INTERP comes from CMakeLists.txt. */
__attribute__((used, section(".interp"))) const char dcap_interp[] = DCAP_INTERP;

/* Entry point for `./main`, selected with -Wl,-e. Three things differ from a
   normal main():
     * No crt0 runs, so this must never return — call exit() instead.
     * argc/argv are not passed in (read /proc/self/cmdline if you need them).
     * The ELF entry point receives a 16-byte-aligned RSP, but a function
       prologue expects the post-call layout (RSP % 16 == 8), so it has to
       realign — otherwise any aligned SSE spill segfaults. */
__attribute__((force_align_arg_pointer)) _Noreturn void dcap_main(void) {
    printf("2 + 3 = %d\n", lib_add(2, 3));
    exit(0);
}
