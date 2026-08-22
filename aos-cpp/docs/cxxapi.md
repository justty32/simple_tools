# C++ API

公開的 C++17 介面由 `<aos/inst.hpp>`、`<aos/format.hpp>` 與
`<aos/exec.hpp>` 宣告，都位於 `aos` 命名空間中。

## 型別

`inst_t` 代表一筆指令。它的公開成員有 `argv`（一個
`std::vector<std::string>`）、`stdin_path`、`stdout_path`、`stderr_path`、
`exit_path` 與 `cwd`（字串）、`env`（一個
`std::map<std::string,std::string>`），以及 `timeout_ms`（`std::uint64_t`，
預設為零）。`clear()` 會把所有成員還原成預設值。

`InstState` 用來回報格式／驗證的結果：`Ok`、`InvalidArgument`、
`JsonSyntax`、`NotAnObject`、`UnknownKey`、`FieldTypeMismatch`、`EmptyArgv`，
以及 `EnvKeyInvalid`。

`ExecState` 包含 `Ok`、
`InvalidArgument`、`SpawnFailed`、`WaitFailed`，以及 `ExitWriteFailed`。
`ExecResult` 包含回報的 `status`、`signalled`、`timed_out`，以及一個
`error`，在相關的 API 失敗時帶著 `errno`。子行程狀態與 API 錯誤兩者的差別，
請參閱[執行語意](exec.md)。

## 函式

`read_all(data, size, out, error_record)` 會解析一整份 JSON 文件。
頂層物件會產生一筆指令；頂層陣列會產生零到多筆指令。它會先清空 `out`，只有在
整份文件與每個物件都有效時，才發佈記錄。發生物件驗證錯誤時，選用的
`error_record` 會收到以一為基底的記錄序號；它一開始被初始化為零，成功、JSON
語法錯誤或無效指標時都維持為零。空輸入是語法錯誤；空陣列
則成功並得到一個空的向量。

`read_one(data, size, out)` 會從這段位元組範圍中，正好解析一個 JSON 值。
它會在檢查或解析之前先清空 `out`。它不接受批次陣列；空緩衝區同樣是語法錯誤。

兩者都**不設任何上限**：位元組數、`argv` 元素數、`env` 條目數、JSON 巢狀深度都
沒有上界。整份輸入都會進到記憶體，所以記憶體用量由輸入大小決定，並由呼叫端的
環境（`ulimit`／cgroup）設界；配置失敗會以 C++ 例外的形式往外傳播，而不是變成
一個 `InstState`。深層巢狀的輸入會讓解析器遞迴爆堆疊而崩潰——請只餵可信來源的
指令檔。

`write_one(inst, out)` 會驗證 `inst`，序列化成緊湊的 JSON 並在最後補上一個 LF，
再把它附加到 `out`。驗證失敗時它不會附加任何東西。維持預設值的選用欄位會被省略。

`write_all(insts, out, error_record)` 是 `read_all` 的反向操作：它把整個批次
序列化成一個緊湊的 JSON 陣列，最後補上一個 LF，再附加到 `out`。空的 vector 會
得到 `[]`，也就是 `read_all` 接受的那個合法空批次。它**先驗證每一筆，才寫出
第一個位元組**——任何一筆無效就完全不動 `out`，並透過選用的 `error_record` 回報
以一為基底的記錄序號（成功時為零）。輸出可以直接餵回 `read_all`。

`execute(inst, result)` 會重設 `result`、驗證 `argv` 非空、準備並執行一個子行程、
等待、視情況寫出它的狀態檔，最後回傳一個 `ExecState`。子行程狀態非零時仍然回傳
`ExecState::Ok`。這個非 const 的指令為 `argv` 提供穩定、可變的字元儲存空間；
呼叫者在執行期間不得變動它。

`to_string(InstState)` 與 `to_string(ExecState)` 會回傳指向靜態、以 NUL 結尾的診斷字串的指標，
且為 `noexcept`。

解析、序列化與執行準備都會用到會配置記憶體的 C++ 容器。配置失敗以及其他未預期的
C++ 例外並不會被這個介面轉譯，而可能往外傳播到呼叫者。上述的輸出保證只適用於回傳的
`InstState` 驗證失敗，不適用於例外。

## 範例

```cpp
#include <aos/exec.hpp>
#include <aos/format.hpp>

#include <cstring>
#include <string>
#include <vector>

int main() {
    const char input[] =
        "[{\"argv\":[\"printf\",\"hello\\n\"],\"exit\":\"status.txt\"}]";
    std::vector<aos::inst_t> jobs;
    std::size_t record = 0;
    if (aos::read_all(input, std::strlen(input), jobs, &record) !=
        aos::InstState::Ok) return 1;

    for (auto &job : jobs) {
        aos::ExecResult result;
        if (aos::execute(job, result) != aos::ExecState::Ok) return 2;
        if (result.status != 0) return result.status;
    }

    std::string encoded;
    return aos::write_one(jobs.front(), encoded) == aos::InstState::Ok ? 0 : 3;
}
```

對著共享函式庫建置，例如：

```sh
c++ -std=c++17 example.cpp -Iinclude -Lbuild -Wl,-rpath,"$PWD/build" -laos
```

## 執行緒與選擇 ABI

各自獨立的 `inst_t` 物件可以並行地使用。`execute` 適合用在多執行緒的行程裡，
因為所有會配置記憶體的環境與 PATH 準備都發生在 `fork` 之前；子行程在 `execve`
之前只使用 async-signal-safe 的操作。對於共用同一筆指令、同一個輸出字串／向量，
或借用來的應用程式狀態，並沒有任何內部的同步機制：不要並行地變動同一批物件，
也不要在一筆指令執行的過程中去改它。

C++ API 與 C API 揭露的是同一套指令、格式、執行結果與上限。若想要直接存取容器，
以及具原子性的多記錄 `read_all`，就選 C++。若想要以 opaque
handle 管理所有權、明確的配置失敗狀態、給 C 或其他 FFI 語言使用，或需要一個穩定的
二進位邊界，就選 C API。

C++ 介面**不**保證跨版本之間的 ABI 相容性。它匯出的簽章與佈局裡含有 `std::string`、
`std::vector`、`std::map`，以及編譯器特定的 C++ ABI 細節。要跨越編譯器、標準函式庫、
語言或版本邊界的程式碼，應該改用 `<aos/aos.h>` 與 C ABI。
