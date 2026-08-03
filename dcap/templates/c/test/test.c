#include <assert.h>
#include <stdio.h>
#include "lib.h"

int main(void) {
    assert(lib_add(2, 3) == 5);
    assert(lib_add(-1, 1) == 0);
    puts("all tests passed");
    return 0;
}
