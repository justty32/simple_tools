# exec_tools 後續規劃：describe

確定性的工具 discovery 已完成在
[`freepy/exec_tools`](../../../../freepy/exec_tools/README.md)：它從 `FREEPY_TOOLS` 或呼叫端明確給定的
目錄讀 `.specs/*.json`，再交給 [`tooljson`](../../../../freepy/llmkit/tooljson/README.md) 執行。

這份只保存尚未實作的 `describe`：讀一個沒有 spec 的執行檔，協助人類產生 tooljson。它不是
discovery 的必要部分，也不應在 agent 每次啟動時自動發生。

## 為什麼延後

文字腳本裡通常已有參數、說明與錯誤處理，LLM 可以讀它並草擬 schema；但這一步同時具有：

- 模型成本與延遲；
- 對參數、side effect 或 exit code 的幻覺風險；
- prompt injection（腳本內容是不可信資料）；
- 二進位檔讀不到，只能參考可能過時的 `--help`；
- spec 過期後何時重產、誰確認的 lifecycle 問題。

因此輸出必須是供人 review 的 candidate，不能因為 JSON 格式合法就自動成為可執行能力。

## 候選流程

```text
missing executable
  → 收集來源（文字內容，或人工提供的說明／--help）
  → LLM 產 tooljson candidate
  → 靜態驗證格式與 target
  → 人工確認參數、side effects、permission、timeout
  → 寫入 .specs/<name>.json
  → exec_tools.scan() / tools()
```

產物仍使用正式 tooljson 格式，不建立第二套 spec。`_extra.source` 保存產生當下的 target 指紋；
`Spec.stale` 只回報來源是否改變，不自動覆寫人修改過的 spec。

## Gate

開始實作前至少要先決定：

1. 文字來源和 `--help` 如何標示 provenance，不能混成模型自己知道的事實。
2. candidate 必須經哪一種明確確認才能寫入 `.specs/`。
3. prompt 與輸出 JSON 的大小上限；binary、secret-looking content 和讀取失敗如何處理。
4. 如何離線驗證「不執行 target、不偷偷掃 PATH、不自動發布 spec」。

在這些問題由真實工具樣本推動以前，使用者直接手寫／修改 tooljson 是較可預測的路徑。
