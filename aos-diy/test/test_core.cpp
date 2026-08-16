#include "check.hpp"

int main() {
  check::ok(1 + 1 == 2, "test 1");
  return check::report();
}
