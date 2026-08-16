#pragma once
// unixcall.hpp — 一次 Unix 呼叫，由 C++ 執行。
//
// 註冊進每一條 loop 的 core env，任務就看得到 unix/call 和 unix/read-status。

#include <janet.h>

namespace aoslisp {

void install_unix_cfuns(JanetTable *env);

}  // namespace aoslisp
