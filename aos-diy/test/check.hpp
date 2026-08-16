#pragma once

#include <iostream>
#include <string>

namespace check {
inline int failures = 0;
inline void ok(bool cond, const std::string &name) {
  std::cout << (cond ? "PASS" : "FAIL") << ":" << name << std::endl;
  failures += cond ? 0 : 1;
}
template <typename A, typename B>
void eq(const A &a, const B &b, const std::string &name) {
  ok(a == b, name);
  if (!(a == b))
    std::cout << "A:" << a << " B:" << b << std::endl;
}
inline int report() { return failures; }
} // namespace check
