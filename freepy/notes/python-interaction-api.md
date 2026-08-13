# Python 互動 API 落地筆記

本頁保存 2026-08-12 至 2026-08-13 從真實 REPL 操作、API 定案到公司 Ollama 驗收得到的
理由與證據。公開用法以 [`shells/README.md`](../shells/README.md) 與
[`llms/README.md`](../llmkit/llms/README.md) 為準。

## 為何拆成 LLM 與 Bot

舊 `LLM(engine, system, tools)` 同時代表 endpoint client 與有狀態的 bot，實際操作時很難用一句
話說清楚誰持有人格、history 與能力。現在固定為：

- `LLM`：endpoint、model、Params 與 caps，只是思考引擎。
- `Bot`：一顆 LLM、system prompt、history，以及完整的 tool schemas／dispatch。
- `Bot.start(instruction)`：用新的 Handle 與背景 runner 啟動一個 Round，立即回 Controller。

`Engine` 暫時是 `LLM` 的 alias；`Assistant`、`assistant()` 與 `session()` 也只作遷移相容層。
它們不是另一套新抽象。

## tools 必須是完整能力

Bot 可接單一 callable、callable 集合或 `(schemas, dispatch)` bundle。建構時立刻檢查名稱重複、
schema／dispatch 名稱是否完全相等，以及 dispatch value 是否 callable。不能讓模型看得到 schema，
runtime 卻沒有相應 effect。

`bot.ask()` 仍是低階的一次模型呼叫，由呼叫者自行處理 requested tools；`bot.start()` 則把 Bot
已驗證的 dispatch 交給 agentloop。擁有工具仍不等於 permission 或 sandbox。

## Round ownership 與換模型

同一個 mutable Bot 的 history 只准一個 active Round 修改。這不只由 `Bot.start()` 檢查；
Handle 初始化也會向 Bot claim ownership，因此不能改用直接 `Controller(bot, ...)` 繞過。
前一個 Round 結束後，可用同一 Bot 啟動下一個 Round並延續 history。

`bot.set_llm(new_llm)` 會保留 system、history 與 tools。操作慣例是只在 `waiting` 或 `paused`
safe boundary 切換；目前 setter 只做型別檢查，尚未從程式上拒絕 `running_step`。下一個值得做的
小改動，是讓換 LLM 的安全邊界成為可驗證契約，而不只是文件慣例。

## 2026-08-13 公司 Ollama live 驗收

直連 `http://192.168.1.146:11434/v1`，只給固定讀取 repository 根 README 的唯讀 callable：

1. `qwen2.5:14b-instruct-q4_K_M` 實際呼叫工具，2 Steps、1 call，回答三個主要區域後停在
   `waiting`。
2. 在 safe boundary 以 Ollama `keep_alive: 0` unload 第一顆模型。
3. `set_llm()` 換成 `qwen3:32b`，沿用四則既有 history，續問哪個區域是 C++23 專案產生器。
4. 第二顆回答 `dcap/`，Round 以 `completed/done` 結束。
5. 最後 unload 兩個 model；`GET /api/ps` 回傳空 models，沒有殘留 VRAM 占用。

測試使用每 Step `max_tokens=4096`，Round 上限為 12 Steps、8 calls、900 秒。公司遠端模型較慢，
後續 live 驗收不要把 max tokens 壓得太小，也要允許多 Step；每次切模前 unload 舊模型，測完
unload 最後一顆。

## Compact 後續作點

優先補 `set_llm()` 的 safe-boundary enforcement 與競態測試：`waiting`／`paused` 可換，
`running_step` 必須明確拒絕，`running_tools` 是否允許則先採保守拒絕。完成後再以相同 Ollama
流程驗收。沒有新的實際需求前，不往 durable service、memory 或 multi-agent 擴張。
