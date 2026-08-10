# 建議架構與語言邊界

## 分層

```text
┌────────────────────────────────────────────┐
│ UX / Python demos / research / provider SDK│
└───────────────────┬────────────────────────┘
                    │ versioned JSON/events
┌───────────────────▼────────────────────────┐
│ Janet semantic core                       │
│ identity · schemas · policy · state        │
│ tool protocol · Round/Step · memory/context│
└───────────────────┬────────────────────────┘
                    │ typed intents/results
┌───────────────────▼────────────────────────┐
│ Trusted adapters / supervisor              │
│ HTTP proxy · fs/db · subprocess · runtime  │
└───────────────────┬────────────────────────┘
                    │ kernel/runtime APIs
┌───────────────────▼────────────────────────┐
│ Linux: namespace · cgroup · seccomp · FUSE │
└────────────────────────────────────────────┘
```

Janet core 可以是主行程，也可以先作 stdio service。關鍵不是誰啟動誰，而是 effect 不能繞過 typed operation 和 supervisor validation。

## Janet 應擁有的 invariant

- agent path canonicalization 與 segment-safe ancestry。
- permission subset、mount downgrade、resource conservation。
- Round／Step／tool call 狀態轉移。
- tooljson schema、argv decision、registry。
- task/report lifecycle 與 idempotency rule。
- memory address、ACL decision、context manifest。
- agentfs resolver 與 per-actor projection decision。

這些是「給定 state/event，唯一決定下一步」的規則，最值得固化。

## Adapter 應擁有的 effect

- HTTP/TLS/SSE、provider credentials。
- atomic rename、fsync、SQLite transaction、object store。
- process spawn、signals、timeout、stdout/stderr drain。
- Podman、namespace、mount、cgroup、seccomp。
- FUSE/9P transport 與 kernel mount。

adapter 不可自行決定授權；它驗證 core 給的 typed intent 是否完整、安全，再執行並產 event。高風險 adapter 最好是獨立、權限縮小的 service。

## Protocol 建議

```json
{
  "v": 1,
  "id": "op-...",
  "actor": "/root/build/tests",
  "instance": "...",
  "grant_generation": 7,
  "deadline": "...",
  "op": "runtime.spawn",
  "args": {"policy_ref": "...", "task_ref": "..."}
}
```

結果包含相同 id、狀態、structured error、evidence refs。`actor`、policy 與 runtime argv 由 supervisor binding 取得，不信任模型在 args 裡自報。

## Plan 9 風格怎麼保留

可以把不同 provider 掛進每個 agent 的私有 namespace：

```text
/self /parent /children /tools /memory /work /srv
```

但內部仍是 typed handle：path 只負責 lookup，open 後的 handle 綁 actor、instance、grant generation。這保留 file protocol 的可組合性，也避免「知道路徑就有權限」。

agentfs 第一版只是 read-only projection API；等 resolver/ACL 稳定，才加 FUSE/9P。控制操作若日後映成 `ctl`，仍轉回相同 typed operation，不另開文字後門。

## Context compiler

source trace、memory object、Step manifest 分離：

- trace 永不因 compact 改寫。
- memory object content-addressed、有 provenance/ACL。
- context compiler 產生下一 Step 的 ordered working set。
- 當前指令與未閉合 tool pairing 必須 inline。
- link load 不提升 authority。

這是 Lisp `quote`／checked dereference 與 OS pager 的交會點，也是最值得用 Janet 寫成小核心的部分。

## 可信計算基底

不要因 Janet core 小就把它稱作完整 sandbox。真正 TCB 包含：Janet core、supervisor、adapter、runtime config 與 kernel。報告建議縮小 TCB，不是假裝語言能取代 container。
