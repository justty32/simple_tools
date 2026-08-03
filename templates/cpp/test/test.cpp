#include <cassert>
#include <cstdio>
#include "lib.hpp"

int main() {
    assert(lib::add(2, 3) == 5);
    assert(lib::add(-1, 1) == 0);
    std::puts("all tests passed");
    return 0;
}
