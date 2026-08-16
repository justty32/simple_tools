#pragma once
// 最小的檢查工具。
//
// ⚠ 用這個而不是 assert：assert 在 NDEBUG 下會**整個消失**，Release 建置的
//   測試就變成空跑還回報通過。這一點跟 aos-core 的 test/check.h 是同一個理由。

#include <cstdio>
#include <string>

namespace check {

inline int checks = 0;
inline int failures = 0;

inline void ok(bool condition, const std::string &name) {
    ++checks;
    if (condition) {
        std::printf("  ✓ %s\n", name.c_str());
    } else {
        std::printf("  ✗ %s\n", name.c_str());
        ++failures;
    }
}

template <typename A, typename B>
void eq(const A &actual, const B &expected, const std::string &name) {
    ++checks;
    if (actual == expected) {
        std::printf("  ✓ %s\n", name.c_str());
    } else {
        std::printf("  ✗ %s\n", name.c_str());
        ++failures;
    }
}

inline void section(const char *title) { std::printf("── %s ──\n", title); }

inline int report() {
    std::printf("\n");
    if (failures == 0) {
        std::printf("%d 項檢查全過\n", checks);
        return 0;
    }
    std::printf("%d / %d 項失敗\n", failures, checks);
    return 1;
}

}  // namespace check
