# FreePy × Ollama 實測：192.168.1.146

測試日期：2026-08-12。這是區域網路開發機的當次快照，不是穩定規格或公開 benchmark。
測試只執行文字生成與純記憶體 `multiply(6, 7)`，沒有讀寫遠端檔案。

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
- Qwen3 32B：先前用 256 output tokens、4 Steps/2 calls 的 smoke 設定時，256 tokens 全在
  reasoning，尚未產生 tool call 就以 `length` 停止。這是預算不足的探針結果，不能判定模型或
  FreePy 工具鏈失敗；原生對照已證明模型會正確 tool call。

## `shells.session()` 互動控制

Gemma 1B 經 FreePy 跑兩輪：第一輪抵達 `waiting`，再呼叫
`Controller.send(prompt, finish=True)`；同一 Handle 最後進入 `completed`，共 2 Steps、沒有錯誤。
這證明新 REPL helper 的 `waiting → send → completed` 真實網路路徑可用。

第二輪把要求保留英文 token 的指令翻成「自由二」。這是 1B 模型的指令遵循品質，不是
Controller 狀態轉移錯誤。

## 結論與下一步

1. Ollama 服務、原生 API 與 OpenAI-compatible API 都可連線。
2. FreePy 對非 reasoning 模型的文字與完整 14B tool Round 已實證可用。
3. `shells.session()` 與 Controller `.send()` 已通過真模型兩輪互動。
4. `llms.Reply` 現已同時讀 `message.reasoning` 與 `reasoning_content`。
5. reasoning 模型的正式測試改用 4096（必要時 8192）output tokens；agentloop 使用至少
   16 Steps/16 calls，只把 Limits 當防無限循環，不拿 smoke budget 評價模型能力。

## 模型切換與卸載規則

重型模型不能只靠正常流程尾端清理。後續測試固定：

1. 每個 case 用 `try/finally`。
2. `finally` 呼叫 `POST /api/generate`，body 為 `{"model": "舊模型", "keep_alive": 0}`。
3. 查 `/api/ps` 確認舊模型消失，才開始下一顆。
4. 測試被取消後也先檢查本機 process 與 `/api/ps`。

本次 4096-token 重測被人工中斷；中斷後已確認本機沒有殘留測試 process，`/api/ps` 的
`models` 為空。該次沒有完整結果，因此不列入能力結論。
