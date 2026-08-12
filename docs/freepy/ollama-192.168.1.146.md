# FreePy × Ollama 實測：192.168.1.146

測試日期：2026-08-12。這是區域網路開發機的當次快照，不是穩定規格或公開 benchmark。
模型只經 Ollama 生成；effects 在 FreePy 所在的本機執行，包括純記憶體 `multiply(6, 7)`，以及
暫存 workspace 內的檔案、exec 與 loopback HTTP 操作，沒有讀寫 Ollama 主機的檔案。
這個位址只在公司網路可用，不能作為常駐測試依賴；家用環境目前只有 Gemma 4 12B 級模型。

## 環境

- Ollama：`0.20.7`，`http://192.168.1.146:11434`
- 原生 API：`/api/generate`、`/api/chat`
- OpenAI-compatible API：`/v1/models`、`/v1/chat/completions`
- FreePy 路徑：`Engine → LLM → Controller/agentloop → Python dispatch`
- 模型皆為 GGUF `Q4_K_M`

| 模型 | 參數量 | Ollama 宣告能力 |
|---|---:|---|
| `gemma3:1b` | 999.89M | completion |
| `deepseek-r1:8b` | 8.2B | completion、thinking |
| `qwen2.5:14b-instruct-q4_K_M` | 14.8B | completion、tools |
| `qwen3:32b` | 32.8B | completion、tools、thinking |

## 原生文字探針

提示要求只回 `MODEL_OK`，`temperature=0`、`seed=42`。這輪刻意使用 64 output tokens，僅驗證
連線與基本指令遵循，不用來評價 reasoning 品質。每顆模型測完後卸載。

| 模型 | 結果 | wall time | load time | prompt/output tokens |
|---|---|---:|---:|---:|
| Gemma 1B | 精確回覆 `MODEL_OK` | 0.43s | 0.17s | 19 / 5 |
| DeepSeek 8B | 精確回覆 `MODEL_OK` | 5.56s | 5.08s | 15 / 3 |
| Qwen 2.5 14B | 精確回覆 `MODEL_OK` | 9.20s | 7.57s | 38 / 3 |
| Qwen3 32B | 精確回覆 `MODEL_OK` | 9.44s | 7.13s | 25 / 3 |

四顆都通過；這台主機能正常冷載入並執行 32B Q4 模型。

## FreePy 單輪

相同類型的精確回覆改走 OpenAI-compatible API 與 FreePy Controller。這仍是低預算探針：

| 模型 | 結果 | wall time | input/output tokens |
|---|---|---:|---:|
| Gemma 1B | `FREEPY_OK`，完成 | 2.14s | 20 / 6 |
| DeepSeek 8B | 空 text，`length` | 10.06s | 12 / 64 |
| Qwen 2.5 14B | `FREEPY_OK`，完成 | 4.45s | 39 / 4 |
| Qwen3 32B | 空 text，`length` | 19.65s | 20 / 64 |

原始 `/v1/chat/completions` 顯示兩顆 reasoning 模型把 token 放在 `message.reasoning`；64 tokens
用完時還沒進入 final content。這不能解讀成模型失敗。測試當時 `Reply` 只讀
`reasoning_content`，因此 reasoning 為空；後續已加入 Ollama 的 `reasoning` alias 並以 OpenAI SDK
解析物件離線驗證，不需重載模型。

## 工具循環

工具 schema 是 `multiply(a: int, b: int) -> str`，真正 effect 只有 Python 計算。模型必須要求
`6 × 7`，agentloop dispatch 後再回答 `TOOL_OK=42`。

### Ollama 原生 `/api/chat`

| 模型 | tool call | 最終答案 | wall time |
|---|---|---|---:|
| Qwen 2.5 14B | `multiply(a=6,b=7)` | `TOOL_OK=42` | 18.05s |
| Qwen3 32B | `multiply(a=6,b=7)` | `TOOL_OK=42` | 25.83s |

兩顆模型的原生工具能力都通過。

### FreePy/OpenAI-compatible

- Qwen 2.5 14B：通過。2 Steps、1 tool call；tool log 正確記錄參數與結果 `42`，最後
  `TOOL_OK=42`。wall time 19.20s，input/output 418/116 tokens。
- Qwen3 32B：4096 output tokens、16 Steps/16 calls 的正式重測通過。2 Steps、1 tool call；
  `multiply(a=6,b=7)` 回傳 `42`，最終精確回答 `TOOL_OK=42`。wall time 73.35s，input/output
  411/244 tokens。先前 256-token smoke 在產生 tool call 前就以 `length` 停止，確認只是預算
  不足，不能拿來判定模型或 FreePy 工具鏈失敗。保存成
  [`ollama_tool_roundtrip.py`](../../freepy/examples/ollama_tool_roundtrip.py) 後再次通過：wall time
  71.95s，input/output 411/234 tokens，且卸載後模型清單為空。

## `shells.session()` 互動控制

Gemma 1B 經 FreePy 跑兩輪：第一輪抵達 `waiting`，再呼叫
`Controller.send(prompt, finish=True)`；同一 Handle 最後進入 `completed`，共 2 Steps、沒有錯誤。
這證明新 REPL helper 的 `waiting → send → completed` 真實網路路徑可用。

第二輪把要求保留英文 token 的指令翻成「自由二」。這是 1B 模型的指令遵循品質，不是
Controller 狀態轉移錯誤。

## Foundation 完整鏈路

以 [`ollama_foundation_roundtrip.py`](../../freepy/examples/ollama_foundation_roundtrip.py) 讓
Qwen 2.5 14B 實際完成一張六步訂單：讀檔、用明確 discovery 找到的 exec tool 加總、用固定
endpoint HTTP tool 查運費、寫檔、改檔、讀回驗證。工具在本機暫存 workspace 與 loopback HTTP
server 執行；模型只經 Ollama 思考。設定為每 Step 最多 4096 output tokens，整個 Round 最多
16 Steps／16 Calls／600 秒。

| 嘗試 | 結果 | Steps/Calls | input/output | wall time | 卸載 |
|---|---|---:|---:|---:|---|
| 原始指令 1 | 工具全通，但檔案是一個含字面 `\\n` 的長行 | 7/7 | 7037/860 | 118.75s | 單次檢查過早誤報 |
| 原始指令 2 | 同一錯誤可重現 | 7/7 | 7037/860 | 119.28s | 輪詢後空載 |
| 明講 physical newline | **完整通過** | 7/6 | 6833/663 | 97.79s | 輪詢後空載 |

通過那輪每種 effect 都真的執行：`read_file` 2 次，其餘 `sum_numbers`、
`quote_shipping`、`write_file`、`edit_file` 各 1 次。HTTP server 收到
`region=TW,total=50`；最後檔案是六個實體行，內容與預期完全相符，Controller 以 `done` 完成且
沒有 error。

前兩輪不是 JSON parser 或 `write_file` 偷改資料：模型傳入的 Python argument 本身就是字面
反斜線加 `n`，工具也明確回報「1 lines」。模型讀回後仍把畫面中的 `\\n` 當換行，宣稱六行驗證
成功。這是可重現的 14B 指令／觀察弱點；在 prompt 明講「真正換行，不是兩個字元」後消失。
它也說明日後 verifier 不能只相信 agent 的自然語言自評，應檢查 effect 產物。

另有兩個工程觀察：`after_step` callback 執行時 Handle 的 state 仍是 `running_step`，所以用
`now()` 印進度會像模型還在想；這是 callback 邊界的呈現問題，不是死鎖。Ollama unload 也不是
保證在 POST 回來那刻立刻反映到 `/api/ps`，測試程式現會短暫輪詢確認，而非只查一次。

## 結論與下一步

1. Ollama 服務、原生 API 與 OpenAI-compatible API 都可連線。
2. FreePy 對非 reasoning 模型的文字、14B foundation Round，以及 32B reasoning 模型的工具
   閉環都已實證可用。
3. `shells.session()` 與 Controller `.send()` 已通過真模型兩輪互動。
4. `llms.Reply` 現已同時讀 `message.reasoning` 與 `reasoning_content`。
5. Controller 加上目前三類 foundation effect 已完成一次有 assertion 的真模型整合測試。
6. reasoning 模型的正式測試使用 4096（必要時 8192）output tokens；agentloop 使用至少
   16 Steps/16 calls，只把 Limits 當防無限循環，不拿 smoke budget 評價模型能力。Qwen3 32B
   已在這組設定下通過。

## 模型切換與卸載規則

重型模型不能只靠正常流程尾端清理。後續測試固定：

1. 每個 case 用 `try/finally`。
2. `finally` 呼叫 `POST /api/generate`，body 為 `{"model": "舊模型", "keep_alive": 0}`。
3. 短暫輪詢 `/api/ps` 確認舊模型消失，才開始下一顆；單次立即查詢可能過早。
4. 測試被取消後也先檢查本機 process 與 `/api/ps`。

Qwen3 32B 的 4096-token 重測結束後已依這套流程卸載，輪詢確認 `/api/ps` 的 `models` 為空。
