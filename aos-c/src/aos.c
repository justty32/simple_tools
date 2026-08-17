#include "aos/aos.h"

#include <stdio.h>

/* Temporary executable behavior while the runtime is built module by module. */
int aos_run(void)
{
    puts("Hello from aos-c!");
    return 0;
}
