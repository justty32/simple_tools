# freepy 的 C／C++ 固化評估

日期：2026-08-10

## 結論

**技術上很可行，但不值得現在把整套 Python 逐行重寫。** 最合適的形狀是：

- **C++23** 擁有穩定的資料契約、狀態機、policy、ledger、path、tool protocol，並逐步接手需要原生 OS 整合的 supervisor／filesystem／process adapter。
- **C** 只作穩定 ABI、極小平台 shim，或不得不接 C library 的邊界；不建議拿純 C 承載整個 agent 語意核心。
- **Python** 繼續當 prototype／research oracle、provider SDK host、`_type:"python"` sidecar 與 callable reflection producer。
- **LiteLLM proxy** 保留為 provider compatibility firewall；C++ client 只面對本機 OpenAI-compatible wire，不重做整個 provider 生態。

若談最終穩定 production runtime，約 **70–85% 的架構責任可合理由 C++ 擁有**；若問「目前就值得搬的已實作行為」，只有約 **35–50%**。前者包含未來的 native process、SQLite、FUSE/9P adapter，後者受限於規格仍在變、Python reflection 與 provider glue。兩個數字是架構範圍，不是 LOC 精算或工期承諾。

純 C 的合理占比很小，約是 ABI／平台 shim 的 **10–20%**。用 C 重寫 JSON-rich、variant-heavy、會持續演化的核心，會把原本清楚的資料語意換成大量 ownership、tagged union、錯誤傳遞與 cleanup code。

## 一句話架構

```text
Python demos / research / provider SDK / _type:python
                         │ versioned JSON / C ABI
                         ▼
C++ semantic core ── typed intents ──► native trusted adapters
path · schema · policy · state          HTTP/SSE · fs/db · process · FUSE
Round/Step · ledger · memory refs                 │
                                                  ▼
                                  Linux namespace/cgroup/seccomp
```

這個方案比 Janet 版本能讓 native 層多接手一些 effect，但代價也更直接：C++ 的 UB、ownership、ABI、thread race 都會進入可信計算基底。**C++ 化不等於安全化**；安全收益只在 supervisor 真正落實 process isolation、namespace、cgroup、seccomp 與 capability reduction 時成立。

## 最先做什麼

第一個切片仍應是 `llmkit/tooljson` 的 pure／exec core，而不是 LLM transport：

1. 從現有 smoke 抽出 15–20 個 versioned golden vectors。
2. C++ 實作 JSON strict validation、registry、argv/stdin decision 與 output clipping；先不做 `_type:"python"`。
3. 用 JSON CLI 同時餵 Python 與 C++，輸出 machine-readable diff。
4. Linux 補 process vectors；Windows pure core 照跑，POSIX exec 明確 fail closed。
5. shadow 一個穩定期後再決定誰成為 default，Python reference 不立刻刪。

這個切片能驗證真正關鍵的事：跨語言契約是否夠精確、C++ dependency/build policy 是否可接受、錯誤與路徑語意是否能差分，而不必先承擔 HTTP、SSE、async 或 sandbox。

## 直覺評分

| 方案 | 判斷 | 原因 |
|---|---|---|
| 全部改純 C | 不建議 | JSON／狀態／plugin／ownership 樣板太重，維護風險高於收益 |
| 全部立刻改 C++ | 技術可行、經濟性差 | provider 與規格仍變，會同時維護兩份未穩定語意 |
| C++ core + native supervisor + Python/proxy adapters | **建議** | 把 invariant、部署與 OS 整合固化，同時保留探索速度與 provider 生態 |
| 完全維持 Python | 短期可行 | 目前規模小且 LLM latency 主導；但長期 runtime／sandbox／嵌入式交付會逐漸吃力 |

## 導覽

- [評估方法與 C／C++ 分工](01-method.md)
- [現有 freepy 模組](02-current.md)
- [規劃中模組](03-planned.md)
- [本機 native 實證與 Janet 對照](04-native-evidence.md)
- [Python demo → C++ 固化流程](05-process.md)
- [建議架構、ABI 與 plugin 邊界](06-architecture.md)
- [分期路線、工期級距與驗收](07-roadmap.md)
- [風險、反證與待決策](08-risks.md)
- [HTML 導覽](index.html)

## 本次實證摘要

- current worktree 約有 **3.35K 行 implementation Python**；含 smoke／輔助程式約 4.4K 行。`agent_runtime`、communication、team、memory、introspection、agentfs 仍是規劃，不能算已完成也不能拿來估「翻譯 LOC」。
- Windows：`agentloop` 35/35、`tooljson` 32/45、`base_tools` 20/26。後兩者失敗都落在明確的 POSIX process/shell 邊界。
- WSL Ubuntu：`tooljson` 45/45、`base_tools` 全過。
- 本機 `dcap` 的既有 C++ scaffolder 可執行；另在 temp 以 CMake 4.3.3 + GCC 16.1.0 做了乾淨產生、configure、build、run，shared library 與 executable 成功，輸出 `2 + 3 = 5`。
- `clang++` 不在 PATH；目前 native 基線是 MinGW GCC。正式 sanitizer／fuzz／Linux supervisor CI 仍未建立。

## 範圍

本報告以 2026-08-10 的 repository 快照為準，評估 `freepy` 程式、`ROADMAP.md`、各
`PLAN.md`、既有 [Janet 固化報告](../lisp-solidification/README.md) 與本 repo 的 `dcap`
C++ 實證。它是架構與遷移評估，不宣稱已有 freepy native implementation。
