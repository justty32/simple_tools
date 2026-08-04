#include <iostream>
#include "lib.hpp"

// The entry point. This is the one file under src/ that does not go into the
// library — everything else there does. It is an ordinary main(): take argc and
// argv if you want them, return when you are done.
int main() {
    std::cout << "2 + 3 = " << lib::add(2, 3) << "\n";
}
