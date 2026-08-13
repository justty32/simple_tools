# Python 參考實作

Python 版本的工作不是暫時拼出 demo，而是把資料格式、失敗語意與測試向量做成日後 C++/Janet
必須通過的 oracle。它是 repo 根目錄下獨立的 `agent-machine/` 專案；FreePy 只是未來可接入的
LLM／tool adapter，不是 Agent Machine 的 package、runtime 或狀態真源。

## 建議 package

```text
agent-machine/agent_machine/
  ids.py              NodeId、FunctionId、RoundId、OperationId
  records.py          frozen records、enum、schema version
  protocol.py         strict JSON encode/decode、error envelope
  paths.py            canonical path、name/.name pairing
  store.py            generation、snapshot、HEAD publish
  transaction.py      staging、validate、commit、abort
  journal.py          prepared/committed/recovery
  namespace.py        resolve/stat/list/read/write providers
  executors/
    base.py  vfs.py  llm.py  tool.py  git.py  model.py
  scheduler.py        queue、lease、resource ledger、policy port
  context.py          ordered input manifest
  gc.py               mark、proposal、quarantine、purge
  cli.py              init/add/run/tick/inspect/recover/gc
  _checks_*.py        package 內的離線 gates
```

`prototypes/agent_machine_m0` 只取 immutable record、reducer replay、operation idempotency 的想法。
它缺 generation CAS、validator、VFS transaction、resource ledger 和測試，不直接 move 成正式 package。

## 現有程式如何重用

| 現有元件 | 第一版用途 | 不可誤認為 |
|---|---|---|
| `llmkit.LLM/Bot/Reply/Params` | `llm.call` adapter、usage、tool-call 格式 | durable Function state |
| `agentloop.advance()` | worker 內一個 Step/tool safe boundary | scheduler 或 recovery engine |
| `tooljson` | strict tool spec、dispatch、結果格式 | permission 或 sandbox |
| `base_tools` | host-FS provider 的 effect 實作 | VFS authority／ACL |
| `exec_tools/http_tools` | 第一批 process/network executor | exactly-once effect |
| Pi bridge | 跨 process protocol 參考 | 核心 state owner |

## P0：pure namespace contract（PR 1）

- 只定義 `Node`、`DirectoryEntry`、`Operation`、`Commit`、closed enum、canonical JSON 與 errors。
- 實作 path parser、stable node id、`name/.name` 配對與 immutable in-memory snapshot/reducer。
- v1 只用一個 root generation；驗 CAS、idempotency 與 stale rejection。
- Function/Round/Instruction records 延到 executor slice，避免第一個 PR 只有一堆空 class。

Gate：同一 operations 得到 byte-identical projection；duplicate operation 不重做；兩個 writer 對同 G
恰好一個成功；rename/move/delete pairing property tests 全過。這階段不宣稱跨 process crash recovery。

## P1：durable directory store（PR 2）

- 加 staging、append-only journal、atomic HEAD publish、recovery 與 host-directory backend。
- 建立 `bot-a/.bot-a` CLI、fsync policy、content hash 和可重現範例 workspace。
- 在 prepared/journal/HEAD/parent-fsync 各點 kill process 後重啟。

Gate：恢復結果只能是完整 G 或 G+1；manifest/hash 不合 fail closed；Windows pure core 可跑，但完整
crash durability 先以 Linux 為 production contract。

## P2：derived Git checkpoint（PR 3）

- 對 `versioned` mount 建 Git commit object，VFS commit 後才發布 managed ref。
- 保存 `checkpoint_pending`，crash/recovery 可補發布；外部 ref conflict 不覆蓋。
- 外部 effect 仍禁用；GC 只做 dry-run proposal。

Gate：Git conflict 不回滾已提交 VFS；checkpoint 缺失可修復；active ref 可 CAS 回先前 revision，但不
改 immutable history 或外部效果。

## P3：單一 Function 接真 FreePy（PR 4–6）

- 先加 generic Instruction/Result 與 deterministic `vfs.*` executor，再接 `llm.call`。
- 每次從 manifest 重建短命 Bot；直接 `LLM.think` + pure Reply parser 是 MVP，`agentloop.advance()`
  只作 compatibility bridge。核心 commit 後才 materialize history/tool pairing。
- `tool.invoke` v1 只 allowlist read-only/deterministic fixture executable。Staging workspace 的 diff 可由
  核心 import，但在 Linux enforcement 前不是 sandbox；任何其他 host effect 都要 explicit approval
  並記 receipt/unknown。HTTP 與 Python callable 延後。
- model lifecycle 是 provider capability；scheduler 以 endpoint/model residency refcount 決定 unload。

Gate：fake endpoint 離線跑完 read -> edit -> verify；crash replay 不重呼 LLM；截斷 tool args 不執行；
每個 assistant call id 都有 committed result 才能進下個 Step。Ollama smoke 用 4096 token profile，
依 `/api/ps` 驗切模與 final cleanup；另測 admission 與 draining race，其他 consumer 持 lease 或同時
acquire 時不得 unload in-use model。

## P4：多 Round scheduler

- durable ready queue、worker lease/heartbeat、late-result rejection。
- token/cost reservation、endpoint slot/rate、exclusive writer 與 cache residency。
- foreground/interactive/background + owner weighted fairness + aging。
- progress、pause、resume、cancel、deadline、backoff 和 reconciliation queue。

Gate：兩個 owner 競爭一個 endpoint slot都能前進；10,000 dormant Functions 不產生 10,000 threads；
429 不形成 retry herd；worker 在 dispatch 前後 crash，帳本不超賣、不雙重退款。

## P5：refs、GC 與受控自我改進

- roots = Function refs + active lease/Round + pin + retention + selected Git refs。
- GC 先 mark/dry-run，再 archive；grace period 後才允許 purge。
- 自我修改一律在 private Git branch；測試與 resource/UX 差異留下 evidence。
- promotion 只原子移動 active ref；失敗立即回舊 ref。外部 effect 預設禁用。

Gate：共享／循環 refs 正確；GC 與同時 commit 競爭不刪 live node；壞版本不能越過 verifier；active
ref 可用一個 CAS 指回先前 versioned revision，不回滾外部效果。

## Python API 與 CLI

最小 API 保持直接：

```python
machine = Machine.open("demo-machine")
fn = machine.function("bot-a")
round_ = machine.start(fn, "do something", budget=Budget(...))
machine.tick(max_dispatch=1)
print(machine.inspect(round_.id))
```

對應 CLI：

```text
python -m agent_machine init demo-machine
python -m agent_machine add demo-machine/bot-a
python -m agent_machine start demo-machine/bot-a -- "do something"
python -m agent_machine tick demo-machine
python -m agent_machine inspect demo-machine <round-id>
python -m agent_machine recover demo-machine
```

`tick()` 是可測的單步驅動；`serve()` 只是反覆 tick 加 wait。測試不依賴 background daemon 或真網路。
