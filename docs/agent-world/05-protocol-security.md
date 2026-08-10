# 檔案協定、能力與隔離

## 節點應像小型 file server

可重用 Plan 9 風格，但保留 typed schema：

```text
<node>/
  .meta/declared.json
  .meta/effective.json
  data
  status
  ctl
  events
```

- `data`：內容或主要 I/O。
- `status`：小型、可快取的當前狀態。
- `ctl`：寫命令，不是直接改 database field。
- `events`：帶 cursor 的 append-only 事件。
- `.meta`：契約、型別、限制與有效能力。

不是每個 node 都要有全部檔案。read-only memory object 沒有 `ctl`；one-shot tool 可用 `clone` 建立 run directory，再在 run 內讀寫。

## provider contract 先於 FUSE/9P

核心介面應能表達：

```text
attach(actor, view)
walk(handle, segments)
stat / list / open / read / write
flush(cancel pending request)
clunk(close handle)
```

另加 implementation 所需的 generation、bytes/list limit、schema 與 audit context。Python resolver、model tools、FUSE、9P 都只是 adapter。

第一版只做 `agent_list`、`agent_read`、`stat`，先驗證 path、providers 和 per-actor view。若一開始就做 mount，ACL、snapshot 與 lifecycle bug 會和 FUSE/9P 細節纏在一起。

## write 是 operation，不是 mutation

```sh
echo 'pause' > /self/ctl
echo 'stop reason=operator' > /team/build/.agent/ctl
```

server 要把內容解析成既有 typed operation，重新做 scope、grant、instance、idempotency 與 audit 檢查。以下 projection 永不直接 writable：

- effective permissions
- resource used/reserved
- task status
- Turn/Round counter
- memory object bytes

可寫 `ctl` 也應使用小型固定 grammar 或 JSON schema，不把自然語言交給另一個模型猜。

## 建立 child/tool run 用 clone transaction

可採 Plan 9 `clone` 的精神，但避免共享 write race：

```text
open /runtime/spawn/clone
  -> server 回 /runtime/spawn/runs/R123
write R123/spec.json
write R123/ctl: commit
read  R123/status
```

每個 open/run 有獨立 identity、expiry 與 owner。`commit` 走 supervisor saga：驗 parent grants、reserve budget、建 instance、發布 registry；失敗時補償且只退款一次。

## path 不是 capability

- 名稱可猜、link 可轉寄、path 可在不同 view 指到不同 object。
- open handle/fid 才綁 actor、instance、rights、grant generation。
- 每次敏感 read/write 仍檢查 revocation；長操作至少在 commit 邊界再檢查。
- 不可用「知道 UUID/hash」當授權。
- list/search 也屬讀取，避免洩漏 private node 存在性。

namespace 提供最小可見面；server-side authorization 提供強制邊界。兩者都要。

## secrets 用 broker，不進 env/memory

借 Factotum 的方向：agent 只拿到「可代表它完成某種認證」的 broker handle，不讀到 raw key/token。

```text
/auth/github/status        brokered
/auth/github/ctl           request scope=repo:read
/auth/github/session       opaque handle / fd
```

是否需要 user confirm 走 tagged interactive tool-input protocol。`/self/env` 只顯示 manifest 明確公開的虛擬變數；宿主 `os.environ`、SSH agent socket、proxy token 預設連名稱都不外洩。

## agentfs 不是 sandbox

即使 agent view 裡沒有 `/host`，惡意程式仍可能直接呼叫 OS syscall、連網或讀真實工作目錄。真正限制要由 runtime backend 執行：

- mount/user/pid/network namespaces 或 container。
- read-only/allowlisted bind mounts。
- cgroup CPU/memory/pids 限制。
- seccomp／Landlock 等 syscall/path 限制（依平台能力）。
- 不掛 Docker/Podman socket，不給廣泛 host credentials。

container 是一個 backend，不是 agent 架構本體。普通 host process、rootless container、microVM 都能承載同一 logical agentfs；status 必須誠實說明實際 enforcement level。

## 慢／快路徑的信任門

in-process tool 失去 crash、memory、syscall 隔離；不能只因「比較快」自動升級。建議：

```text
可信 ∧ ABI/schema 支援 ∧ 已證明是熱邊 -> in-process
其他                                     -> exec/container
```

九軸證書與 observed reliability 可作為信任門輸入，但 operator policy 才能授權。撤照時回退 exec，不刪工具資產。

## 最低安全不變量

1. canonical path 經 parser，拒絕 `..`、symlink escape 與 segment-prefix 錯判。
2. effective permissions 永遠是 parent delegation、runtime support、task scope 的交集。
3. secret 不出現在 projection、log、summary 或 denial message。
4. stale instance handle 不能讀到同 path 的新 agent private state。
5. projection provider error 一律 fail closed。
6. 任一 write 最終對應可審計的 typed operation。
7. 沒有 sandbox 時不得回報「已隔離」。

