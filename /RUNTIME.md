# AgentOS runtime 契約

本頁把 FreePy 的 Handle／Controller 轉成 Linux 上可跨 process、可恢復的做法。它是 Python prototype
的第一個實作目標；公開操作仍以 [`INTERFACE.md`](INTERFACE.md) 為準。

## Bot 成員只是薄入口

`agentos new` 產生的每個公開成員都是 POSIX sh wrapper。以 `start` 為例：

```sh
#!/bin/sh
bot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
exec agentos call "$bot" start "$@"
```

`status/run/llm/messages/tools` 只替換 member 名稱。path 與 argv 分開傳，不拼 shell command；Bot path 有
空白仍可用。`agentos call BOT MEMBER ...` 是內部共用入口，Python module path 不進生成檔。日後
`/usr/bin/agentos` 從 Python launcher 換成 C++ binary，既有 Bot 不必重建。

## Handle 是檔案，不是 Python object

每個 Bot 只有一份目前工作。它的 state、pending input 與 event log 保存在 `.agentos/`；公開命令不傳
Run ID 或 path。完成後，新 `start` 才把舊紀錄封存。

state 只保存 JSON 可表達的資料。禁止放 Python callable、thread、open file、client object 或 exception。
LLM/tool 正在執行時所需的輸入，先保存成 immutable instruction；worker 回來後只提交 immutable result。

最小 private layout：

```text
.agentos/
  state.lock
  worker.lock
  current/
    state.json
    journal.jsonl
    results/
  archive/
  worker.log
```

lock 永遠鎖固定檔案，不鎖會 atomic replace 的 state.json。這套 crash guarantee 只適用 Linux local
filesystem；WSL 的 state 不放 `/mnt/c`，也不宣稱支援 NFS。

## Controller 是短命 worker

Python v1 不先做常駐 daemon。自動工作由 Bot 自己的一個 detached worker process 執行：

```text
start / continue
      │
      ├─ durable state 已保存
      └─ spawn agentos worker BOT
                    │
                    ├─ 一次 LLM 或一個 tool
                    ├─ commit result
                    ├─ 看 pause / stop / waiting
                    └─ 需要時再做下一件事，否則退出
```

- worker 用新 process session，terminal／SSH 關閉不會送它 SIGHUP。stdin 接 `/dev/null`，stdout/stderr
  不繼承 client 的 pipe／TTY，診斷寫 private worker.log；除 handshake 外關閉 inherited file descriptors。
- parent 與 child 用一個小 pipe 握手；child 成功取得執行權後，`start -b` 才回 0。
- foreground `start` 也用同一個 detached worker；它只是另外執行 `run wait`。
- Ctrl-C 只中斷等待 client 並回 130，worker 繼續。它不暗中 pause 或 stop。
- paused、waiting 或 terminal 時 worker 退出，不保留 parked thread／process。
- `continue` 重新 spawn worker；waiting 的 `send` 在 auto mode 也會重新 spawn。
- `next` 同樣 spawn detached one-shot worker，client 只等待；worker 取得執行權、做一條 instruction 後退出。
  因此 client 的 Ctrl-C 不會直接 kill 已 dispatch 的 LLM／tool。

spawn 前不可持有 state lock。child 以 `start_new_session` 啟動，取得 worker lock 後才透過唯一保留的 pipe
回 handshake。durable accept 到 handshake 完成前，client 暫緩 SIGINT；之後 Ctrl-C 才只 detach。

spawn／handshake 失敗時，已保存的 work 不會偷偷刪除：state 保持 ready（`next` 時保持 paused），append
`worker_start_failed`，命令回 1 並提示 `run continue` 或重試 `run next`。handshake 之後 child 立刻 crash
仍可能讓工作停住；下一次 status/continue 依下方規則恢復。v1 不承諾無人操作的 process/reboot 自動續跑。

日後可把 worker 換成 per-user C++ scheduler 或 systemd user service；Bot path、state 格式與 CLI 行為不變。

## 兩把 Linux lock

每個 Bot 只需兩種 `flock`：

```text
state lock       很短：排序 command、append event、replace state
worker lock      較久：保證同時只有一個 executor
```

state lock 不可跨 LLM、tool、等待或 pipe output 長時間持有。worker lock 可以跨多條自動 instruction，
但每條 instruction 之間都重新取得 state lock、查看最新 control request。

automatic worker 與 `run next` 都必須先取得 worker lock。取得失敗就回 `busy`，不直接讀 state 後冒險
執行。`status` 與設定 view 是 read-only；修改與控制 command 都經 state lock。

任何可能改變 effective `engine`／`endpoint`／`model` 的 `llm set/unset/use`，以及 `llm unload`，也必須先
取得同一把 worker lock，並持有到 lifecycle 操作與 config commit 全部結束。如此 paused 時另一個 shell
同時執行 `run continue`／`run next` 只會得到 busy，不可能在 unload 中途啟動 LLM instruction。單純修改
temperature 等 request parameter 不做慢 lifecycle 操作，只需 state lock 排序。

需要兩把以上 lock 時固定順序為：

```text
worker lock -> model resource lock -> state lock
```

可以先短暫取得 state lock 做 read/validate，但取得 model lock 前必須釋放，之後 commit 時重新驗證。
任何路徑都不能拿著 state lock 等 worker/model lock。

model mutation 的固定流程是：拿 worker lock；短暫讀出舊 config 與 candidate 後放開 state lock；拿舊 model
的 exclusive resource lock；再拿 state lock 確認 config 未被別人改、仍無 in-flight/pending call；放開 state
lock 後 unload；最後再拿 state lock 重驗 config 與相同安全條件，才 atomic commit candidate。整段都持有
worker/model locks。
失敗時 config 不變；即使舊 model 已成功 unload，下一次 LLM call 仍可依舊 config 重新 load，不形成半套設定。

## command 的唯一順序

多個 shell 同時操作同一 Bot 時，誰先取得 state lock，誰先 append event。event log 中遞增的 `seq` 是
唯一可觀察順序；wall-clock timestamp 只供人看，不決定結果。

例如 `send` 與 `stop` 競速：

- send 先入 log：message 保存，之後 stop。
- stop 先入 log：Run 已 terminal，send 被拒絕。

pause、continue、stop 重複呼叫是安全 no-op。start、send、skip 會增加資料，不能在不知道前一次結果時
盲目重送；v1 不為本機 CLI 增加公開 request ID。若未來有網路 client，再加 optional retry key。

instruction 落盤、pause／stop wait 與 crash repair 的完整順序見 [`RECOVERY.md`](RECOVERY.md)。

## C++／Janet 邊界

C++ 版本必須重用同一個 state transition、lock scope、JSON/JSONL 與 subprocess tests。Janet/Lisp 只可
針對 immutable snapshot 回傳 typed decision；不能持有 lock、直接寫 state，或讓 VM object 變成 Handle。
