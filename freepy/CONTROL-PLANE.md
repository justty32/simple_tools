# 大量 bot 的 control plane：可行性筆記

日期：2026-08-10

這份保留 scale 可行性與介面推導；執行模型已升格為
[Agent Machine 正式規格](agent_machine/README.md)，實作順序以其
[`PLAN.md`](agent_machine/PLAN.md) 與 [ROADMAP.md](ROADMAP.md) 為準。

## 判斷

bot 從磁碟喚醒、待命、沉睡，以及 leader 的控制流程見 [`BOT-LIFECYCLE.md`](BOT-LIFECYCLE.md)。

「一萬個 bot，不等於一萬個常駐 agent process」這個判斷成立。bot 的 system prompt、tool JSON 多半是不可變資料，history 是持久化、持續追加的資料；只有 Round 活躍時，才需要取出上下文並送出 Step 給 thinking endpoint。冷 bot 可以只是一筆磁碟上的 record。

所以整體更像 OS：它能管理很多 process records，但同時只讓少量工作佔用 CPU。freepy 要管理大量 bot records，讓有限 endpoint slot 一次處理一小批 Step。Claude/Codex/Pi 適合當幾個 terminal，並不適合成為 kernel。

這在架構上可行，但有兩個修正：

1. 瓶頸不只有網路。endpoint 的並發、token/minute、prompt 大小與遠端推理時間通常最重；本機 tool 仍可能吃 CPU、RAM、磁碟、網路，甚至啟動 build／browser。
2. bot 資料可冷藏，不代表把一萬個 `agentloop.run()` 都建成 asyncio task 就免費。queued Round 也應持久化；只有拿到資源 lease 的工作才進 runtime。

## 三種不同的東西

```text
Bot record    身分、system、history ref、tool specs、policy；可長期 dormant
Round record   一次目標與控制生命週期；queued/running/paused/done
Step attempt 一次 endpoint request -> 完整 response/error；真正的 thinking 排程單位
```

Round 是使用者看見的工作單位，Step 是 endpoint scheduler 的 dispatch 單位。工具發生在兩個 Step 之間，走另一組 tool worker 資源；不計 Step，但要計 calls、CPU/時間及副作用。

## 建議執行模型

```text
             endpoint A queue -- semaphore / rate budget --+
runnable Round                                               +-> Step
             endpoint B queue -- semaphore / rate budget --+
                                                               |
                       response calls tools                     v
Round done <- final response <- runnable again <- tool queue/workers
```

- **N 個 bot records**：大多不載入記憶體。
- **M 個 queued/running Rounds**：以 durable records 保存，不用一 Round 一 thread。
- **K 個 active Steps**：K 受 endpoint/model 的 concurrency、rate limit 與 token budget 限制。
- **T 個 active tool jobs**：獨立限制，避免一堆 shell/build 把 thinking scheduler 拖死。

一個 Step 結束後更新 history 與 Round record：若有工具，送 tool queue；工具結果完整落盤後，Round 才重新成為 runnable。若是 final response／error／budget，進 terminal state。這沿用目前的 Round／Step／tool batch 語意，而不是另造一套 loop。

## scheduler 最小語意

第一版真正需要的不是高級分散式系統，而是以下可驗證契約：

- per-endpoint/model 的最大並發與最小間隔；遇 429/5xx 有明確 backoff。
- priority + fair queue，避免一個團隊或超長 Round 永久吃滿 endpoint。
- dispatch 前發 lease；完成後以 attempt id 原子提交，逾時 lease 可回收。
- 同一 bot 同時只准一個會修改同一 history branch 的 Round，或明確 fork branch。
- instruction、pause、stop 在既定 safe boundary 生效；不假裝能撤銷已送出的 Step。
- tool worker 另有 root、allowlist、calls/time 與 concurrency policy。
- crash 後可從 records 判斷 queued、leased、settling、done；副作用工具不可盲目重跑。

要特別防「thundering herd」：endpoint 恢復時不能讓數千 Step attempt 同時 retry。rate budget 與 jitter 應屬 endpoint scheduler，不散落在每個 bot 裡。

## 原生介面不是另一套 chat UI

### Python API／REPL

這是最接近 kernel API 的介面，也是第一個應做的原生入口。REPL 適合建立／查詢 bot、啟動 Round、檢查 record、人工修復；它直接拿 typed object，不需 parse 畫面文字。REPL 只是呼叫 API 的環境，不應另有一套只在 REPL 成立的狀態。

### shell

shell 適合人在 terminal 操作，也適合 script 編排。命令可先保持小：

```text
freepy bot list [--team ...] [--state ...] [--json]
freepy round start <bot> --prompt-file ...
freepy round status <id> [--watch]
freepy round tell|pause|resume|stop <id> ...
freepy round result <id>
freepy attach <window-agent>
freepy endpoint status
```

預設輸出給人讀；`--json`／JSONL 才是穩定機器介面。清單必須可 filter/page，不能把一萬個 bot 全塞進一個 TUI tree。

### Plan 9 式 filesystem

這是很合適的最終原生 projection，但應建立在 typed operations 與 state machine 之上，而不是先拿檔案寫入語意反推核心。例如：

```text
/agents/<id>/status        read snapshot
/agents/<id>/history/      immutable entries
/agents/<id>/inbox         append instruction
/rounds/<id>/status         read
/rounds/<id>/ctl            write pause/resume/stop
/endpoints/<name>/queue    read
/teams/<id>/members        read
```

它的好處不是「用 GUI file manager 點 bot」，而是所有語言都已會 `open/read/write/watch`，shell 也能直接組合。難點是權限視圖、原子寫入、阻塞 read/watch、snapshot 一致性與錯誤回報；因此先有同一組 typed operation，再做 FUSE/9P/projection。普通檔案不能成為第二份 source of truth。

## 現成 agent 的位置

Claude Code、Codex、Pi 不是被放棄，而是變成 operator terminal：

- 人與領頭／窗口 agent 在其成熟 UI 對話。
- 它經 MCP／extension 查 team status、向某個 Round 追加指令、讀報告、啟停工作。
- 背景 bot 不各自啟一份 CLI，不各自維持 TUI/session process。
- 領頭 agent 也不能繞過 scheduler 直接把幾千個 Step 同時送 endpoint。

換句話說：借它們的 UX，但 scheduling authority 與 durable truth 留在 freepy。

## 已收斂的下一步

不打斷 `agentloop` 的既有修正順序，但 scheduler 的純資料模型現在可以獨立開始。第一個 scale
prototype 已列為 Agent Machine M0 + M1：用 fake endpoint 跑「一萬個 dormant bot records、
幾百個 queued Rounds、固定 K 個 active Steps」，驗 resource conservation、fairness、late result
與有界 active memory；不開一萬個真 CLI，也不先做 dashboard、HTTP 或 FUSE/9P。
