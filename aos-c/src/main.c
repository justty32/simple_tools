#include "aos/aos.h"

/* Keep process startup separate from the reusable aos-c entry point. */
int main(void)
{
    return aos_run();
}
