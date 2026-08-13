# AgentOS Run 限制

LLM、messages、tools 是 Bot 資料；限制屬於這次 Run，不增加第四個 Bot 成員。

## v1 只設一個硬限制

```sh
./start --max-calls 64 "do something"
```

`max-calls` 計算 Agent Machine CPU instructions：

- 一次 LLM call 算 1。
- 一次 tool call算 1，AgentOS 內建的 `ask_user` 也一樣。
- status/control command 與 terminal pairing closure 不算。

預設是 64。這比 agent loop 常見的小輪數寬鬆，又能防止錯誤的 auto loop 無限花 token 或反覆執行 tool。
`max-calls` 必須是正整數，start 時保存進 Handle state；後續不受全域設定變動影響。

達到限制時不猜工作成功或失敗。runtime 在下一個安全邊界變成：

```json
{
  "status":"paused",
  "mode":"step",
  "reason":"limit",
  "used":{"calls":64, "llm":20, "tools":44, "tokens":81234}
}
```

使用者可以停止，或明確增加額度：

```sh
./run more 32
./run continue
```

`more N` 只在 `paused + reason: limit` 接受，增加 N 次，仍保持 paused；它不等於批准下一個 tool，也不
偷偷 continue。step mode 的 `run next` 也受同一限制。

兩個入口分工固定：工作開始前要改初始上限才用 `start --max-calls N`；沒指定就用 64。工作真的碰到上限
後，只能用 `run more N` 增加剩餘額度。`more` 不能降低額度，也不能拿來設定下一個 Run。

## 邊界順序

runtime 每次 dispatch 前先確認 `used.calls < max-calls`；`dispatching` event 落盤時才占用一次額度。
只有 `prepared`、尚未 dispatch 的 crash recovery 不扣額度。若已有 `dispatching`，即使 crash 發生在真正
送出前，恢復時也保守算一次，不為了拿回額度冒險重送。

一條剛好用完額度的 instruction 仍會完整 commit，再依以下順序決定狀態：

1. stop、fatal error 或 outcome unknown 先進 terminal，並補齊 pending calls。
2. 普通 assistant 最後答案直接 `done`，不因剛好碰到上限改成 paused。
3. 已執行的 `ask_user` 進 `waiting`；回答是 control/result，不多算 call。回答落盤後若還需下一條 instruction，
   才變成 `paused + reason: limit`。
4. 其他情況只要仍需下一條 instruction，就變成 `paused + reason: limit`。

因此最後一次 LLM 若只是「提出 ask_user/tool call」，那個 tool 還沒有執行，狀態是 limit pause；先 `more`，
才能 `next` 或 `continue`。最後一次額度若已用來執行 ask_user，狀態則是 waiting，使用者仍能回答。

## 其他數字放在哪裡

- 每次 LLM 最大輸出：`llm.parameters.max_tokens`，live default 4096。
- 每次 LLM timeout：`llm.timeout`。
- exec tool timeout／輸入輸出上限：見 [`EXEC.md`](EXEC.md)；其他 executor type 各自訂自己的契約。
- prompt、completion、cached、reasoning、total token usage：每次 event 與 status 都記錄。

v1 不先做總 token 硬上限，因為 endpoint 只在一個 call 結束後才可靠回 usage，硬上限仍可能超過一個
request。也不先做 wall-clock Run deadline，paused／waiting 的人類時間不該和真正 executor time 混在一起。

未來中央 scheduler 可用已保存的 call/token/time、foreground/background、模型確定性與資源占用來決定
不同 Bot 的先後順序。那是 Controller 的 policy，不改 `start/next/pause/continue/stop` 的基本意思。
