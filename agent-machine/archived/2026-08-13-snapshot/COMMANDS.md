# AgentOS 指令契約

本頁補足 [`INTERFACE.md`](INTERFACE.md) 中容易含糊的地方。它描述目標行為，不代表 Python
prototype 已全部做到。

## 一個 Bot，一個目前工作（已確認）

v1 不公開 Run ID。`./run` 永遠操作這個 Bot 的目前工作；工作結束後，它也能查看最近一次結果。
新的 `./start` 會把舊的完成紀錄移入 private archive，再建立新工作。

`messages` 屬於 Bot，不屬於單次工作。每次已完整提交的 LLM message 與 tool result 都會保存，所以下次
`start` 預設延續對話。要重開對話，先在沒有 active Run 時執行 `./messages clear`。

## `start` 的三種方式

```sh
./start "TASK"          # 自動執行，呼叫端等待答案
./start -b "TASK"       # 自動執行，呼叫端立刻返回
./start --step "TASK"   # 只建立工作，停在第一步之前
./start --max-calls 128 "TASK"  # 少用：覆蓋本次工作的安全上限
```

- 前景與背景只決定 shell 是否等待，不改變工作的生命週期。
- runtime 擁有執行權；前景 client 的 Ctrl-C 只停止等待，不等於 `pause` 或 `stop`。
- `start -b` 的 stdout 為空，簡短的 started 訊息走 stderr；要讀狀態用 `./status`。
- `start --step` 建立後是 `paused`，尚未呼叫 LLM 或 tool。
- `--max-calls N` 只設定這次 Run，預設 64；碰到上限的精確順序見 [`LIMITS.md`](LIMITS.md)。
- 已有 active Run 時，三種 `start` 都拒絕。它不猜使用者想覆蓋哪個工作。

狀態只看 `./status`。`./run` 沒帶 subcommand 時顯示短 help；v1 不另設 `run status`。

## 狀態與指令

active states 是 `ready`、`working`、`waiting`、`paused`；terminal states 是 `done`、`stopped`、
`failed`。`idle` 表示還沒有可看的工作。`ready` 通常很短暫，表示 runtime 可以領取下一件事。

| 指令 | idle／terminal | ready | working | waiting | paused |
|---|---|---|---|---|---|
| `start` | 建立新工作 | 拒絕 | 拒絕 | 拒絕 | 拒絕 |
| `run next` | 拒絕 | 拒絕：先 pause | 拒絕：busy | 拒絕：使用 `send` | 做一件事；ask_user 會進 waiting，其餘仍 paused |
| `run pause` | 無 active Run | 立即 paused | 當前工作結束後 paused | 保持 waiting，mode 改 step | 成功，不重複動作 |
| `run continue` | 無 active Run | 設為 auto，確保 worker 已啟動 | 成功，不重複動作 | 保持 waiting，mode 改 auto | ask_user 回 waiting；否則轉為自動執行 |
| `run send TEXT` | 拒絕 | 拒絕：先 pause | 拒絕：先 pause | 回答 ask_user；依 mode 自動或 paused | 無 pending call才加入；否則拒絕 |
| `run skip REASON` | 拒絕 | 拒絕 | 拒絕 | 拒絕：問題需要回答 | 有 pending tool 時略過並保持 paused |
| `run more N` | 拒絕 | 拒絕 | 拒絕 | 拒絕 | reason=limit 時增加額度，仍 paused |
| `run stop` | terminal 時成功 | 立即 stopped | 當前工作結束後 stopped | 立即 stopped | 立即 stopped |

重複 `pause`、`continue`、`stop` 是安全 no-op。

`stop` 的「立即」包含本地 closure：pending tool／ask_user 依序補 `Not run: stopped by user.`，再發布
stopped。它不執行 tool、不算 instruction、不呼叫 LLM；下一個 `start` 可安全延續 history。

## `next` 的精確意思

`run next` 只在 `paused` 使用。若 `next: tool`，它就是使用者同意執行眼前這一個 tool；若
`next: llm`，它呼叫一次 LLM。它只做一條 Agent Machine instruction：

- 一次 LLM call；或
- 一次 tool call。

`ask_user` 也是一條 tool instruction，只是使用 AgentOS 內建 executor，不啟動外部 process。它執行後進入
waiting，再用 `send` 回答。這保持「一次 LLM call 或一次 tool call就是一條 CPU instruction」的一致模型。
若 `reason: limit`，`next` 拒絕並提示先用 `more N`；限制不能靠切成 step mode 繞過。

如果一次 LLM call 要求三個 tools，之後需要三次 `next`。每一條 instruction 都完整保存結果後才返回，
不留下半個 message。完成後通常仍是 `paused`；若得到最後答案則是 `done`，ask_user 則是 `waiting`。
tool 確定回傳錯誤仍是一則合法 result：step mode 保持 paused，auto mode 繼續讓 LLM 看見錯誤。只有
無法形成合法 result、外部 outcome unknown、LLM call 完全失敗或 state invariant 壞掉，Run 才是
terminal `failed`。

auto mode 也用相同單步邊界，只是 runtime 自動領下一條；手動與自動不是兩套 loop。

## waiting、send 與 skip

`waiting` 只有 Bot 明確呼叫 `ask_user` 這一種來源：

```json
{"status":"waiting", "need":"message", "prompt":"Which file?"}
```

- 只能用 `send` 滿足 waiting。
- 一般 pending tool 會顯示 `paused + next: tool`，用 `next` 執行或用 `skip REASON` 拒絕。
- `skip` 不呼叫 tool；它保存一則「user skipped」的 tool result，讓 messages pairing 仍然合法。
- 一次只處理下一個 pending tool；三個 tools 可以逐一 `next` 或 `skip`。
- working 時不接受 `send`。先 `pause` 再送，避免新 message 穿插在 tool call 與 tool result 中間。
- paused 但仍有 pending tool 時，也要先 `next` 或 `skip`，之後才能 `send`。

Run 另保存簡單的 `mode: auto | step`。普通 `start`／`start -b` 是 auto，`start --step` 與 `run pause`
是 step，`run continue` 轉回 auto。因此 waiting 收到 `send` 後：auto 會繼續，step 仍 paused。使用者
明確 pause 後，`send` 永遠不解除 pause。

前景 `start`／`run wait` 若有 controlling TTY，會把 ask_user 問題寫到 stderr，並從 `/dev/tty` 讀一行後
走同一個 durable `send`；它不讀 pipeline stdin。沒有 TTY 時只提示所需命令並保持 waiting，讓另一個
process 執行 `run send`。

## pause、stop 與中途操作

LLM request 或 tool process 一旦送出，`pause`／`stop` 不假裝能倒帶。若目前正在 `working`：

1. 記下 `after: pause` 或 `after: stop`。
2. 等這一條 instruction 回來並保存結果。
3. 不再送出下一條 instruction。

若連線中斷，無法判斷有副作用的 tool 是否完成，工作進入 `failed` 並保存 `outcome: unknown`；runtime
不自動重送。未來若需要強制終止，另設明確的 `kill`，不讓溫和的 `stop` 突然變成危險操作。

## 修改 Bot 成員

`llm`、`messages`、`tools` 的讀取與 `check` 隨時可用。修改只允許在沒有 active Run，或 active Run 處於
`paused`／`waiting` 時；`ready`／`working` 會回覆 `pause first`。每個成員仍會
檢查自己的條件；例如 waiting 必定有 pending `ask_user`，所以不能 clear messages 或改 system。

- 修改在下一條 instruction 生效，也成為後續工作的預設值。
- active Run 要追加 user message 時，用 `run send` 留下完整控制紀錄。
- 有尚未取得 result 的 tool call 時，不准移除該 tool，也不准清除相關 messages。
- 有 pending tool call 時也不切換 model；先用 `next` 完成或用 `skip` 留下合法 result。
- `set` 只寫本 Bot 的 local override；`$ref` 指向的共用檔不會被改。
- 切換 engine／endpoint／model 時，command 在回成功前先安全 unload provider 支援的舊模型，再保存新
  config；新模型到下一條 LLM instruction 才載入。共享模型正被其他 AgentOS Bot 呼叫時不可誤卸載。

`run pause` 一旦接受就把 `mode` 設為 step；即使當前 instruction 稍後才完成也不會自動領下一件事。
`run next` 永遠保持 step，做完後不偷偷自動繼續。只有明確的 `run continue` 會把 mode 改回 auto。

直接修改 `.agentos/` 是 unsupported。公開指令負責 schema check、atomic write 與 log。

## 輸出與 exit code

stdout 只放答案或 JSON，進度與人類錯誤走 stderr。`run wait` 只在 terminal state 返回；waiting 時會
提示所需動作並繼續等。完整的 default／`--json`、JSONL 與 exit code 規則見
[`OUTPUT.md`](OUTPUT.md)。
