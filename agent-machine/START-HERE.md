# AOS：從這裡開始手寫 C++

這是目前的工程交接入口。完整設計仍在 [`full/`](full/README.md)，但第一次動手不需要先讀完全部文件。

## 現在停在哪裡

P1a-2 Phase 2 的**本輪窄切片已結束**。Python 工作台目前有 **74/74 tests**，其中 16 項直接涵蓋 process 路徑；它已把一棵兩段式 Task tree 的第一個假 child 換成真 Linux process，第二個 child 仍是固定結果的 fake。

這裡的 Python 是 **可執行參考模型**：可以直接跑測試、觀察落盤結果，並拿來和 C++ 比對的行為基準。它不是 C++ ABI（跨語言固定介面），也不是要求照抄的類別、模組或檔案架構。

本輪已證明的核心是：

```text
先提交 dispatch intent（即將造成外部作用的持久意圖）
-> 真正啟動一次 process
-> 唯讀驗證 process 留下的原始證據
-> 證據完整才提交 Task Receipt（AOS 的完成收據）
-> 證據不完整就停住，不自動再啟動
```

尚未完整驗證：P0 三個 hook 的完整矩陣、十段 root failpoint、已知的 nonzero／signal／spawn error、process golden bytes、斷電、多 writer、NFS、TOCTOU 安全、正式 scheduler 與 Agent。

精確證據與限制以 [Phase 2 工作台 README](workbench/2026-08-14/p1a2-process-python/README.md)為準。

## 第一個 C++ 落點

先只實作 **process evidence producer（process 原始證據產生器）**，不要同時重寫整個 Runtime。

最小垂直切片：

1. 接受已驗證的 absolute executable path、argv、raw stdin、cwd 與明確 environment policy。
2. 不經 shell 建立 process；正確處理 fd、`CLOEXEC`、stdin／stdout／stderr 同步推進，以及 exit／signal／launch error。
3. 對輸入與輸出設硬上限；超限要明確失敗，不能先無界配置再拒絕。
4. 產生可被既有唯讀 binder 驗證的 P0 raw evidence。
5. C++ 不直接寫 parent／child relation、Task events 或 Task Receipt；這些仍由 Runtime authority 提交。
6. 用同一組 fixture 對照 Python，先比 termination 與 raw bytes，再談正式可攜格式。

第一版最有價值的完成條件不是「C++ 能跑一個命令」，而是：在 success、nonzero、signal、launch error、雙大量輸出與中斷案例中，C++ 不會死鎖、不會遺失證據，也不會讓 recovery 重做可能已發生的作用。

## 建議閱讀順序

1. [`full/00-STATUS-AND-SOURCES.md`](full/00-STATUS-AND-SOURCES.md)：先確認不可違反的名詞與安全規則。
2. [Phase 2 工作台](workbench/2026-08-14/p1a2-process-python/README.md)：看目前實際跑過什麼。
3. [`07-P1A2-PROCESS-SEAM.md`](workbench/2026-08-14/round-2-decision/07-P1A2-PROCESS-SEAM.md)：看 process、attempt、binder 與 Receipt 的責任邊界。
4. [`08-P1A2-FORMATS.md`](workbench/2026-08-14/round-2-decision/08-P1A2-FORMATS.md)：需要對照工作台 bytes 時再讀；格式不是正式 ABI。
5. [`full/12-IMPLEMENTATION-AND-EVIDENCE.md`](full/12-IMPLEMENTATION-AND-EVIDENCE.md)：看 Python、C++、Janet 的建議分工。
6. [`full/options/09-CPP-JANET-BRIDGE.md`](full/options/09-CPP-JANET-BRIDGE.md)：只有開始碰跨語言 policy 時才需要。

想先用較輕的方式複習整體模型，可讀 [`wait_user/`](wait_user/README.md) 的繁中濃縮包。

## 重跑目前基準

在 repository root 執行：

```sh
cd agent-machine/workbench/2026-08-14/p1a2-process-python
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v
```

測試資料夾會建立在 Linux `/tmp`。若 source 位於 Windows 磁碟，仍應從 WSL 執行，且不要把 store 或 filesystem crash tests 放在 `/mnt/c`。

另有較早的 C++23 process 基準：

```sh
cd agent-machine/workbench/2026-08-14/p0-function-cpp
cmake -S . -B /tmp/aos-p0-cpp-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/aos-p0-cpp-build -j
ctest --test-dir /tmp/aos-p0-cpp-build --output-on-failure
```

開始寫 C++ 前先重跑兩組基準。之後每次只替換一個責任，保留 Python 作對照，避免同時改 process semantics、保存格式與上層排程而失去問題定位能力。

## 不要從原型偷定的事

- Python 類別、JSON 欄位、UUID、目錄名稱與事件名稱都不是產品 ABI。
- `Function` 不等於 process；process 只是最底層 leaf 形式之一。
- `Task` 不等於 PID；一次 process 啟動只是 Task 內的一次 attempt（嘗試）。
- P0 的 `receipt.json` 是 process staging evidence，不是 AOS Task Receipt。
- nonzero、signal 與 launch error 可以是「結果已知」；unknown 指證據不足，不能混成普通失敗。
- `fsync`、rename 與 process-kill 測試不能直接外推成斷電、NFS 或 exactly-once 保證。

目前最安全的開工方式，是把 C++ 放在 Linux 硬邊界，逐步取代 evidence producer；等差分測試穩定後，再決定哪些 durable commit 也由 C++ 接手。
