# 本機 native 實證與 Janet 對照

## 已證明的 native 基線

本 repo 的 `dcap` 已是小型 C++23 程式：

- `std::filesystem`、`std::variant`、`std::expected`、RAII cleanup。
- CMake executable + shared library 交付。
- Windows／Linux／macOS-oriented template。
- `#embed` 內建模板，外部模板仍可覆寫。

本次不只沿用舊 binary；另在 Windows temp directory 實際執行：

```text
dcap cpp native_probe
cmake -S native_probe -B native_probe/build -G "MinGW Makefiles"
cmake --build native_probe/build
native_probe/bin/native_probe.exe
```

結果：CMake 4.3.3 偵測 GCC 16.1.0，shared library 與 executable 均建成，程式輸出 `2 + 3 = 5`。temp probe 已移除，workspace 未留下 build artifact。

這證明目前機器能乾淨建立基本 C++ 專案；**沒有證明** JSON、HTTP/SSE、process supervisor、sanitizer、fuzz 或 freepy ABI 已選型。`clang++` 目前不在 PATH，也還沒有 Linux native CI。

## Python reference 的平台證據

- Windows：agentloop 35/35、modelcards 31/31。
- Windows：tooljson 32/45，13 個 exec case 因 POSIX fixture 不是 Win32 executable 失敗。
- Windows：base_tools 20/26，6 個 shell case按設計拒絕非 POSIX。
- WSL Ubuntu：tooljson 45/45、base_tools 全過。

因此 native contract 應明定「pure core 跨平台、production process Linux-first」，而不是為了讓表格全綠就稀釋 shell／argv 語意。

## C++ 可以合理採用的能力類別

以下只是候選能力，不是已鎖 dependency：

| 能力 | native 路徑 | 選型前必驗 |
|---|---|---|
| JSON | C 或 C++ parser + explicit validator | duplicate keys、number range、UTF-8、ordering、unknown fields |
| HTTP/TLS/SSE | 成熟 native HTTP client，或先用 Python transport | incremental SSE、cancel、proxy、TLS store、backpressure |
| Storage | filesystem + SQLite C API | fsync/rename、transaction、crash replay、busy policy |
| Process | POSIX API／native library wrapper | argv/env、pipe drain、timeout、signal tree、partial output |
| Async | explicit event loop／scheduler | ownership、cancel、deadline、thread affinity |
| FUSE/9P | native adapter／獨立 service | per-session identity、kernel callback、unmount/crash |
| Testing | unit/property/fuzz + sanitizers | clean Linux CI、ASan/UBSan/TSan matrices |

不要在報告階段先選滿整套 framework。第一個 tooljson spike 應用最少 dependency，量 build size、compile time、API ergonomics、fuzz integration，再寫 ADR。

## 與 Janet 方案的差異

| 面向 | Janet semantic core | C++ core／runtime |
|---|---|---|
| data/reducer 表達 | 很精簡、動態 | 型別更強、樣板較多 |
| TLS/SSE/process/FUSE | 多數需外部 adapter | 可由同語言 native adapter 擁有 |
| embedded scripting／REPL | 強 | 弱；需另留 script/plugin 邊界 |
| ABI／library 交付 | 通常 CLI/embed Janet | C ABI、static/shared library 成熟 |
| 記憶體安全 | VM 管理，多數錯誤可控 | UB、lifetime、allocator、race 直接進 TCB |
| build/dependency | 輕但生態較小 | 生態廣，但 CMake／ABI／套件管理成本高 |
| Windows 本次證據 | subprocess 曾 access violation | 基本 C++ build 過；runtime 尚未證明 |

Janet 更像「小而可塑的 semantic machine」；C++ 更像「可一路延伸到 OS effect 的 production substrate」。如果主要目標是概念簡潔與可重寫規則，Janet 有吸引力；如果目標是 library 嵌入、單一 native supervisor 與 kernel integration，C++ 更合適。

## 單一 binary 的現實

C++ 可以把 defaults、schema 或少量 card embed 進 binary，但 freepy 仍需要模型 runner、LiteLLM/provider config、workspace、credentials、tool specs 與可能的 container runtime。把所有資料 `#embed` 只會讓每次 card／policy 更新都要重編。

建議形狀是：binary 內建最低安全 defaults 與 protocol version；外部 versioned data 可更新，經 hash／signature／validation 載入。不要把「有一個 exe」誤認成「沒有運維依賴」。
