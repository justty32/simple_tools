# 風險、反證與待決策

## 已知風險

| 風險 | 目前證據 | 對策 |
|---|---|---|
| 把可編譯誤認為語意穩定 | 多 agent 模組仍只有 PLAN | 只移 contract 已凍結的 slice |
| memory safety／UB 擴大 TCB | C++ 將進 parser、thread、OS callback | RAII、無裸 ownership、ASan/UBSan/fuzz、process isolation |
| thread race 被固化 | `Handle` 宣稱跨 thread但沒有 lock | single-owner event queue、線性化 fixtures、TSan |
| C++ ABI／allocator 崩潰 | plugin/host 可能跨 compiler/CRT | public C ABI；opaque handle；allocator 成對；不跨界丟 exception |
| dependency/build 複雜 | native HTTP/JSON/async 尚未選型 | 最小 spike、lock/version/license、clean CI、ADR |
| Python dynamic plugin 失去相容 | `_type:"python"` 依 importlib/callable | Python sidecar或 exec/RPC，不假裝 native 等價 |
| provider churn 滲入 core | OpenAI SDK/LiteLLM 正在吸收差異 | 保留 proxy firewall；core 只認 normalized events |
| 單一 binary 幻覺 | runner/config/credentials/tool data 仍外部 | embed safety defaults，不 embed 所有 mutable data |
| 安全被過度宣稱 | path containment與 native compile都非 sandbox | Linux supervisor落實 namespace/cgroup/seccomp |
| Windows 支援被誤推 | basic MinGW build 過，但 POSIX tests fail | pure core跨平台；runtime Linux-first；Windows另立 gate |
| 雙實作漂移 | 尚無 shared golden harness | differential CI；一個 stable period後明確 owner/fallback |
| 效能收益被誇大 | workload由 LLM/tool wait 主導 | 先 baseline；以部署/RSS/invariant為主，不用空泛 speed claim |

## 最重要的反證

本次能乾淨 build/run C++，只證明 toolchain 可用；不證明 freepy native 化值得。現有 Python implementation 只有數千行、依賴單純，且主要 latency 來自模型與工具。若目標只是讓聊天快，全面重寫幾乎沒有商業理由。

C++ 的真正理由應至少中一項：

- 需要嵌入其他 native host／提供穩定 library。
- 需要大量 agent 的低 idle RSS／快速啟動。
- 需要一個長期 Linux supervisor 直接整合 process/cgroup/FUSE。
- 需要把 policy/state machine 變成 fuzzable、versioned、可重播的 production owner。

若沒有上述 consumer，`tooljson` 第二實作做到方法驗證後即可停止。

## 安全風險

- C++ parser、SSE、FUSE callback 都是 attacker/model-influenced input；必須當 hostile bytes fuzz。
- core 不持有 Podman socket、host root或 provider secret。
- raw argv、mount、network destination 在 supervisor 從 template重建，不信 model payload。
- path不是 authority；open後的 handle 綁 actor、instance、grant generation。
- memory link不是 grant；list/search也不能洩漏 forbidden name。
- crash/retry 不能重扣 budget、重跑 tool、重複退款或重複完成 task。
- C ABI caller傳入 pointer/length都需完整驗證；callback不得在已釋放 core上重入。

## 待決策

### 1. C++ 的部署角色

選項：CLI/stdio sidecar、shared library、主 agent process。建議順序也是如此；先 sidecar取得差分與 crash isolation，確有 latency需求再加 C ABI，最後才讓它當主行程。

### 2. C++ 標準與 toolchain

本機已有 C++23/GCC 16.1/CMake 4.3.3證據。正式支援應定最低 GCC/Clang/MSVC與 stdlib，Linux CI至少雙 compiler；不要讓 `#embed` 這類非核心功能抬高整套 runtime門檻，除非確有收益。

### 3. Exception policy

建議 core內可局部使用 exception或 result，但 public C ABI、thread entry、plugin與 OS callback一律轉成 typed error；destructor不得丟。需以 ADR固定，不要各 module自選。

### 4. Dependency policy

JSON、HTTP、async、test各選一個最小 owner；禁止同時引入多套 value/json/event type。決定 vendoring、FetchContent或 system package前，先驗 offline/reproducible build與 license。

### 5. Windows 支援等級

建議 v1：pure core與library build支援 Windows；shell/process/container/FUSE production只承諾 Linux。若要 Windows runtime，另做 CreateProcess/Job Object/UTF-16/path/cancel矩陣。

### 6. Python 相容期限

Python reference至少保留一個 stable release；provider SDK、research、`_type:"python"`可永久存在。對每個被替換 slice指定 removal metric，而不是追求零 Python。

### 7. 設定要不要 embed

只 embed protocol version、最低安全 default與必要 recovery schema。modelcards、tool specs、policy與proxy config保留外部 versioned data；載入時驗證與hash，避免每次研究更新都重編 binary。

## 最終判斷

**值得做 C++ vertical slice，不值得做全面 C/C++ rewrite。** 最佳長期形狀是 C++23 semantic core + Linux native supervisor，以 C ABI／versioned event與外界連接；Python與LiteLLM保留在它們最有優勢的探索、reflection與provider邊界。

第一個 checkpoint 是 tooljson pure core。它若能以小 dependency、乾淨 build、shared fixtures與 fuzz穩定通過，再擴到 modelcards／identity／agentloop；若做不到，就保留 Python，這個失敗成本仍然很低。

## 決策記錄建議

每個 slice 留一份短 ADR：

- 為什麼此刻 contract 足夠穩定。
- Python oracle、fixtures與undefined行為。
- C++擁有的 invariant與adapter effect。
- ownership/thread/error/ABI policy。
- dependency與platform support。
- benchmark、fallback與移除條件。
