# Round、Turn 與 tick

## 三個時間尺度

| 名稱 | 邊界 | 主體 |
|---|---|---|
| **Round／輪** | 一次 `ask(input) -> model message` | 模型 |
| **Turn／回合** | agent 因使用者指令啟動，經多輪與工具，直到主動停止 | 單一 agent |
| **tick** | 收集指定世界/群組的行動與介入，產生下一個可觀察局面 | supervisor／world |

Round 和 Turn 是目前 agentloop 的必要概念；tick 是多 agent 或 OS-as-game 才需要的可選上層。不要把 tick 直接改名為 Turn，它們的同步範圍不同。

## 一個 Turn 的內部

```text
user instruction
  -> Round 1: ask -> tool calls
  -> tool execution/results
  -> Round 2: ask -> more calls
  -> additional user instruction (同一 Turn)
  -> Round 3: ask -> final message, no tool call
  -> Turn stops voluntarily
```

- 模型 reasoning 若包含在一次 ask 內，仍只算一輪。
- tool 執行在兩輪之間，不自成 Round。
- 使用者在 active Turn 加指令，Turn 延續；下一次 ask 才形成新 Round。
- 工具內部要求使用者輸入，是該 tool call 的互動事件，不是 user message，也不增加 Round。
- supervisor 強制 stop/cancel 不是「主動完成」，要記不同 stop reason。

## tool input 的獨立通道

```text
tool call ──need_input(id, schema)──> user
          <──input(id, value)────────
          ──tool result──────────────>
```

這條通道要有 correlation id、schema、timeout、cancel 與 audit，但不寫進 model role history 當新指令。若答案本身需要模型理解，tool result 可包含結論，下一輪自然看見。

## tick 解決什麼

多 agent 各自有獨立 Turn；不能拿某一個 agent 的「主動停止」當全隊完成。tick 可作為較慢的協調層：

```text
tick(scope, current_position, user_interventions)
  -> 啟動/喚醒範圍內 NPC/agents
  -> 等待完成、checkpoint、deadline 或 policy stop
  -> 收集 reports/artifacts/events
  -> commit next_position
```

一個 tick 可以包含：

- 多個 agent 各一個或多個 Turn。
- 一個 agent 的多個 model Rounds。
- 使用者在執行期持續介入。
- 完全確定性的 scripts/tools，沒有任何 model Round。

因此 tick 是 world state transition，不是聊天計數器。

## 巢狀 tick 與組織路徑

父群組的一次 tick 可以驅動 child group 多次 tick，例如 `/root/research` 等兩個 researchers 完成各自探索，再向父層交一份 report。巢狀時必須顯式定義：

- scope：這一拍涵蓋哪個 subtree/task，不由 path 自動擴權。
- barrier：all、quorum、deadline 或 first-success。
- propagation：哪些 child artifacts/events 往上冒。
- cancellation：父 stop 是否遞迴、child 能否 checkpoint。
- resource accounting：child budget 從父 reserve，不因 nested tick 複製。

不要讓父 tick 直接偷看 child private context；child 應透過 report、shared memory refs 或 task artifacts 交付。

## 事件與狀態檔

建議 live projection：

```text
/self/state
/self/turn
/self/round
/self/stop-reason
/self/events
/self/wait
/team/ticks/<tick-id>/status
/team/ticks/<tick-id>/members/<agent>/state
```

`events` 是 append-only cursor stream；`status` 是當前 projection。需要「當時發生什麼」讀 event log，需要「現在怎樣」讀 status，需要一致畫面讀 generation-bound snapshot。

`wait` 若加入阻塞 read，必須支援 cancel/flush；模型工具 v1 仍宜用有限 timeout 的 poll/check，避免一個無限 wait 吃掉整個 Turn 控制權。

## context 與時間

每個 Round 有自己的 context manifest；每個 Turn 有 source trace 與 working memory；每個 tick 有跨 agent 的 position/report snapshot。三者的 ref 可以互指，但不可互相取代：

```text
Round manifest -> 我這輪看見什麼
Turn trace     -> 我這次活動做了什麼
tick position  -> 這群主體共同把世界變成什麼
```

這個分層也讓 replay 比較誠實：重播 Round 只需固定 model input；重播 Turn 還要固定 tool/environment；重播 tick 則要固定多 actor schedule、介入與外界狀態。

