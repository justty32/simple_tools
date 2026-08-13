# AgentOS 使用場景檢查

本頁用實際情境檢查 [操作介面候選](INTERFACE.md)，目前仍待使用者確認。

## 1. 建立與設定

```sh
agentos new bot-a
cd bot-a
./llm set model qwen3.5:9b
./llm set temperature 0.1
./llm check
./tools add ../shared/read-tool.json
./messages set system "reply briefly"
```

共用設定可以拆檔：

```sh
./llm use ../shared/ollama.json
./llm set temperature 0.1       # 仍只寫 Bot local override
```

API key 可用 `$env`。AgentOS 保存 env 名稱，不把 secret 寫回檔案。

## 2. 一次性前景工作

```sh
answer=$(./start "summarize README")
```

stdout 只輸出最後答案，進度走 stderr，方便 shell 組合。自然完成後 messages 已保存；下一次
`start` 可以延續既有對話。

## 3. 背景長工作

```sh
./start -b "inspect this project"
./status
./run wait
```

關閉 terminal 或中斷觀看不等於 stop。真正停止一定要明確 `./run stop`。

## 4. 手動逐步檢查

```sh
./start --step "fix the bug"
./run next       # 一次 LLM
./status
./run next       # 第一個 tool
./run next       # 第二個 tool
./run continue   # 之後自動跑
```

這讓使用者能在任何兩條 Agent Machine CPU instructions 之間把關。

`status` 預設只顯示當下真正有用的幾行，例如：

```text
bot: bot-a
status: paused
mode: step
next: tool read_file {"path":"README.md"}
```

若下一個是 `ask_user`，step mode 先顯示 `next: tool ask_user`，再一次 `run next` 才進 waiting；auto mode
會自行做到 waiting。前景 `start`／`run wait` 會在 controlling terminal 詢問，回答仍透過同一個 durable
`send` transition 保存。

## 5. 暫停、修改、繼續

```sh
./run pause
./status
./llm set temperature 0.2
./tools remove dangerous_tool
./run send "do not change production files"
./run continue
```

這個例子先真正 pause，避免設定與 worker 競速。沒有 active Run，或 active Run 處於 paused／waiting 時，
才可做符合各 member 安全條件的修改；切換 model 時先安全 unload 舊模型。若 status 顯示 pending tool，
上面的 remove／send 仍會拒絕：先 `run next` 執行，或 `run skip "reason"` 補齊 result，再修改。

## 6. 使用者追加要求

```sh
./run pause
./status
./run send "also test Linux"
./run continue
```

working 時先 pause，避免新 message 插進 tool call 與 result 之間；paused 時 `send` 只保存，不自動
continue。若 status 顯示 pending tool，上面的 `send` 會拒絕；先用 `run next` 執行或 `run skip "reason"`
略過，再 send。工作已 done
時拒絕，使用者改用新的 `./start "also test Linux"`；因 Bot messages 已保存，對話仍能延續。

## 7. 停止與錯誤

```sh
./run stop
./status
./run log
```

stop 永久結束目前工作，但不撤銷已完成的 tool effect。LLM/tool 中途失聯且結果不明時顯示 failed，
不自動重送可能有副作用的操作。新工作由使用者再次 `start`。

若 stop 時還有 pending tool 或未回答的 ask_user，AgentOS 只在本地補一則 `Not run: stopped by user.` result，
不真的執行它。這使 messages 保持完整；下一個 `start` 能看到舊工作為何停下。

## 8. 多個 Bot

```sh
./bot-a/start -b "task A"
./bot-b/start -b "task B"
```

AgentOS runtime 可以同時控制不同 Bot。v1 同一 Bot 若已有 active Run，再次 `start` 會清楚拒絕。
需要兩條平行分支時先建立／複製另一個 Bot。等真實需求證明值得，再加入公開 Run objects 與 merge，
不讓 v1 先背負 Handle ID 和 history conflict。

## 9. Runtime 重啟

Run state 與 log 都在 `.agentos/`，不是 Python memory。runtime 重啟後，ready／waiting／paused／terminal
可直接恢復；paused／waiting 不保留一條 parked thread。

原本 working 的 instruction 必須先讀 journal：確定未送出才可回 ready，已有完整結果就完成提交，無法
判斷外部操作是否發生則轉為 `failed` 並顯示 `outcome: unknown`。它不會永遠卡在 working，也不會猜測後
重做；使用者看 `run log` 後再開始新工作。

## 10. 未來換實作語言

生成的 `start/status/run/llm/messages/tools` 只把 Bot path 與 argv 交給 `/usr/bin/agentos`。因此：

```text
現在：shell entry → Python prototype
未來：shell entry → C++ core → Janet/Lisp policy
```

私有資料使用有版本的 JSON/JSONL。禁止把 pickle、Python callable、thread object 或 mutable Handle
當持久格式。C++ 與 Lisp 版本重播同一批 CLI golden tests；外部用法完全不變。
