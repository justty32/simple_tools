# 最終 C++23／Janet 架構

Python 契約穩定後再固化，不逐行翻譯。C++ 負責 machine mechanics，Janet 負責可調 policy；
提交權只在 C++。

## 責任切分

| C++23 擁有 | Janet 擁有 | 獨立 adapter/process 擁有 |
|---|---|---|
| ids、path、schema validator | queue 候選的排序／選擇 | provider SDK、LiteLLM |
| VFS transaction、journal、recovery | retry/switch/escalate policy | Python callable reflection |
| generation CAS、refs、GC mechanics | context 選頁政策 | 高風險 tool process |
| ledger、lease、scheduler mechanics | GC retention／promotion 建議 | HTTP/TLS/SSE、外部 service |
| permission check、commit、audit | 自我改進 acceptance 規則 | Linux sandbox supervisor |
| process lifecycle 與 native adapters | 可列出、可熱換的設定規則 | FUSE/9P transport（較後期） |

Janet 取得 immutable snapshot，回傳帶 `snapshot_generation + policy_version + decision_id` 的
`PolicyDecision`。C++ commit loop 重新檢查候選集合、預算、權限與版本；protected-call error，或
獨立 Janet process crash/timeout 時採安全預設（通常 pause/reject），絕不放寬規則。

## Process 輪廓

```text
agent-machine (C++ executable)
  one commit/event loop
  read-only snapshots
  worker pools by executor class
  one trusted embedded Janet policy env on its owner thread
        |
        +-- stdio/local socket --> Python LLM/provider sidecar
        +-- typed request ------> restricted tool workers
        `-- OS API ------------> Git/fs/process adapters
```

每個 Round 有單一邏輯 event queue；UI、timer、network callback 和 worker completion 只能 enqueue。
一條 commit loop 依序驗證並發布 generation，避免多把鎖保護同一個巨大 object graph。慢 I/O 全在
worker；snapshot 可並行讀。量測證明 commit loop 是瓶頸前，不做多 writer/shard。

Python sidecar 使用有長度前綴、版本化的 request/result frame，至少帶 request/instruction/attempt id、
deadline、payload bytes/hash 與大小上限。stdout 只跑協議、stderr 記 log；dispatch 後 EOF 是
`outcome_unknown`，先 reconcile，不能普通 retry。Secret 經受限 FD/OS store 傳入，不寫 JSON/journal。

## C++ core 形狀

```cpp
struct Transition {
    MachineState next;
    std::vector<Intent> intents;
};

expected<Transition, Error>
reduce(const MachineState& state, const Event& event);
```

`MachineState` 是 value semantics，不要求每次深拷貝整棵樹；實作可用 immutable content-addressed
blocks/structural sharing。跨 ABI 與 journal 仍使用 canonical bytes。

Public operation 接 immutable value、回 value/error，getter 不做 I/O。`Event`、`Intent`、狀態和
錯誤使用 closed `std::variant`；provider-specific body 留在 versioned payload/registry。跨 process
先用 canonical JSON v1；只有 benchmark 證明必要才換 binary codec。

不把 STL type、exception 或 allocator 跨 ABI。若需提供 library API，外層是 versioned C ABI：

```c
typedef struct am_core am_core;
typedef struct { uint32_t abi; const uint8_t *data; size_t size; } am_bytes_view;
typedef struct { uint8_t *data; size_t size; } am_owned_bytes;
int am_step(am_core *, am_bytes_view, am_owned_bytes *out, am_owned_bytes *error);
void am_owned_bytes_free(am_core *, am_owned_bytes *);
void am_core_free(am_core *);
```

成功／失敗時未使用的 output 必為 `{nullptr,0}`；buffer 只能交同一 library/version 的 free 釋放。
ABI 另定 calling convention、visibility、reentrancy 與「destroy 等 in-flight call 歸零」。跨 process
優先用 stdio protocol，避免 cross-CRT allocator 問題。

## Janet 嵌入規則

`langlab-janet` 已驗證 `janet_init -> janet_core_env -> janet_dostring -> janet_deinit` 及 native module
註冊。正式版在 process startup/shutdown 各 init/deinit 一次，使用 owner-thread 專用 env、protected
call 與預載 policy module，不在每次決策 `dostring`。

Embedded Janet 只跑受信任、經審查且工作量有界的 policy。Janet cooperative cancel 不能停止純 CPU
無限迴圈，C++ 也不能安全 kill 持有 VM heap 的 thread。需要不受信任、熱換或硬 deadline 的 policy，
必須放進受限 process，逾時 kill 整個 process並拒絕 decision。v1 embedded policy 禁止 unbounded loop、
blocking I/O、FFI、thread 與 spawn。

- Janet runtime 只初始化一次，policy env 只能由 owner thread 進入；不能被多個 C++ worker同時 call。
- 跨 thread 只傳 immutable request/result；Janet `ev/chan` 不跨 thread，`thread-chan` 會 marshal copy。
- v1 policy 不拿 C++ object pointer。opaque value 只存 `{handle_id,generation,capability}`；native call
  由 owner thread 查 registry 後短暫取物件。Close 先 unpublish，再排空 owner queue 才釋放；Janet
  finalizer 只清 handle record，不能 delete core object。
- 跨界字串／buffer 一律 copy 成 Janet-owned 或 C++-owned value；不長存 Janet buffer 的 data pointer，
  因 buffer grow 會使地址失效。
- Janet FFI 配置的長生命記憶體沒有自動 GC 保證；v1 不讓 policy 自行 malloc 或持有 OS handle。
- Janet C API error 與 C++ exception path 必須隔離。Native cfun 使用窄 C shim：執行可能 non-local error
  的 `janet_get*`／`janet_fixarity` 時，不跨任何 C++ RAII frame、lock、transaction 或 owning pointer；
  shim 先解成 owned POD，再呼叫捕捉全部 exception 的 C++ implementation。Janet signal/stacktrace 由
  protected call 序列化，絕不穿越 C++ core。
- C++ core 不跨 call 保存 `Janet`、`JanetBuffer*` 或 `JanetTable*`。若 adapter 需要暫存 Janet value，
  必須正確 root；v1 直接禁止這種設計。
- Janet 看不到 raw filesystem、socket、secret 或 commit pointer；需要效果只能回 typed intent。

多 env/多 runtime 的同 process 安全性在 pinned Janet 版本做過 stress test 前，不作平行承諾；需要
多核或隔離時使用多 process。Reload 建新 process/env，在安全邊界切 version，不重用 global table。

## Build 與部署

- CMake 管整個 C++ host；Janet 固定版本、以 third_party static library 連入。
- `jpm` 只用於 Janet 開發／測試，不作最終 C++ 產品的部署真源。
- policy bundle manifest 記 Janet runtime version、target/arch、source/module-graph hash 與 policy schema；
  startup 明確設定 module path並驗證。Hash 只保證完整性／重播，不代表 sandbox 信任。
- Linux 是完整 production target：GCC + Clang、ASan/UBSan/TSan、fuzz、pidfd/cgroup/sandbox tests。
- Windows 先保證 pure core、VFS contract、Python differential tests；process isolation 不可假稱等價。
- CI 必須從空 build directory configure/build/test；另測 install 後沒有開發樹也能啟動。

`langlab-janet` 已記錄 jpm 漏追非 entry source、Windows DLL 被 REPL/LSP 鎖住、subprocess access
violation 等問題，因此 clean build、module hash 與 process smoke 都是 gate，不採「build 顯示成功」
作唯一證據。

## 從 Python 遷移

1. 凍結一批 v1 JSON vectors、journal traces、crash points 與 expected projection。
2. C++ CLI 先以 shadow mode 跑相同輸入，輸出逐欄 diff；不接真 LLM。
3. C++ 接手 path、validator、transaction、replay，再接 ledger/scheduler。
4. Janet policy 先 shadow Python policy；差異需能分類為 spec、bug 或允許的 policy change。
5. 穩定期內 Python 仍是 oracle；C++ 成 default 後也保留 corpus runner。
6. 最後才接 native process/Linux enforcement 與可選的 FUSE/9P。

暫停固化的條件：protocol 仍頻繁改、差分主要暴露規格空白、sanitizer/race 無法維持綠，或第二份
實作沒有部署／可靠性收益。此時繼續 Python，不為語言偏好永久雙維護。
