#pragma once
// vm.hpp — 一條 loop 的 Janet VM。
//
// ★ 這個類別的每一個成員函式，包含建構與解構，都**必須在同一條執行緒上**呼叫。
//   Janet 的 VM 狀態是 thread-local，在 A 執行緒 janet_init()、在 B 執行緒
//   janet_deinit() 是未定義行為。所以 Vm 只在 Loop::run() 的堆疊上建立，
//   生命週期就是那條執行緒的生命週期——沒有別的地方拿得到它。
//
// 這個檔是整個外掛裡唯一 include <janet.h> 的地方之一（另一個是 unixcall.cpp），
// loop.cpp 完全不認識 Janet，plugin.cpp 也是。

#include <string>

#include "job.hpp"

namespace aoslisp {

class Vm {
  public:
    Vm();
    ~Vm();

    Vm(const Vm &) = delete;
    Vm &operator=(const Vm &) = delete;

    // bootstrap 有沒有跑起來。false 的話 boot_error() 說原因，run() 會直接回報失敗。
    bool ok() const { return ok_; }
    const std::string &boot_error() const { return boot_error_; }

    // 求值 job.source，填好 job.out／job.err／job.status。不丟例外。
    void run(Job &job) noexcept;

  private:
    bool ok_ = false;
    bool started_ = false;
    std::string boot_error_;
    void *run_job_ = nullptr;  // JanetFunction*，已經 gcroot
};

}  // namespace aoslisp
