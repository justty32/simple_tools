#include <stdio.h>
#include "lib.h"

/* The entry point. This is the one file under src/ that does not go into the
   library — everything else there does. It is an ordinary main(): take argc and
   argv if you want them, return when you are done. */
int main(void) {
    printf("2 + 3 = %d\n", lib_add(2, 3));
    return 0;
}
