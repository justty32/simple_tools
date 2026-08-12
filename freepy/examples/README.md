# examples

可執行的 FreePy 使用範例，不是穩定 API。

- [`llm_tool_roundtrip.py`](llm_tool_roundtrip.py)：串流、reasoning 與工具呼叫的完整 roundtrip。
- [`ollama_tool_roundtrip.py`](ollama_tool_roundtrip.py)：對任意已安裝 Ollama 模型執行可重跑的
  最小工具閉環 probe；模型與 host 由命令列指定，結束時會卸載模型。
- [`ollama_foundation_roundtrip.py`](ollama_foundation_roundtrip.py)：對任意已安裝 Ollama 模型
  實測 Controller、檔案、exec discovery 與固定 endpoint HTTP 的完整鏈路。

## Ollama 手動 probe

Ollama 並非 FreePy 的常駐依賴；這兩支是有人確認服務與模型可用時才執行的網路整合測試，
不會加入離線回歸。`--model` 必須是 `/api/tags` 已存在的精確名稱，probe 不會自動 pull。
最小工具閉環預設連本機 Ollama：

```sh
cd freepy
uv run python examples/ollama_tool_roundtrip.py --model gemma4-12b
```

公司區網主機則明確傳入 host 與該機器已有的模型：

```sh
uv run python examples/ollama_tool_roundtrip.py \
  --host http://192.168.1.146:11434 \
  --model qwen3:32b
```

完整 foundation 鏈路使用相同參數規則，但任務較長，且模型必須能可靠使用五種 effects：

```sh
uv run python examples/ollama_foundation_roundtrip.py \
  --host http://192.168.1.146:11434 \
  --model qwen2.5:14b-instruct-q4_K_M
```

啟動前若 Ollama 已載入任何模型，probe 會拒絕執行，以免干擾別的工作。執行後無論成功或失敗
都會要求卸載本次模型並輪詢確認；stdout 最後只有一份 JSON report，進度寫到 stderr。成功時
exit code 為 0，連線、assertion 或清理失敗皆為非零。

可能被推翻、用來探路的程式放在 [`../prototypes/`](../prototypes/README.md)。
