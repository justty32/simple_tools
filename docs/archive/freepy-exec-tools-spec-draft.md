# exec_tools 舊 spec 草案

> **歷史文件，不是目前格式。** 正式格式與執行契約見
> [`../llmkit/tooljson/README.md`](../../freepy/llmkit/tooljson/README.md)。目前格式使用真正的 OpenAI
> tool JSON 加 `_extra`，而不是下列攤平結構。

只有 OpenAI schema 不足以表達命令列呼叫慣例。schema 能說 `width` 是整數，卻不能說它要
寫成 `--width 800`、`-w 800`，或第二個位置參數。早期草案因此把 schema 和呼叫慣例放在一起：

```json
{
  "name": "resize",
  "description": "把圖片縮到指定寬度，蓋掉原檔",
  "exec": "/home/x/tools/resize",
  "parameters": {
    "type": "object",
    "properties": {
      "path":  {"type": "string",  "description": "要處理的圖片路徑"},
      "width": {"type": "integer", "description": "目標寬度，像素"}
    },
    "required": ["path"]
  },
  "argv": [
    {"param": "path",  "kind": "positional"},
    {"param": "width", "kind": "option", "flag": "--width"}
  ],
  "stdin": null,
  "source": {"size": 1843, "mtime": 1754460000, "sha256": "3f9a…"}
}
```

`argv` 的每一項把一個參數映射到命令列位置：

| kind | 產生的東西 | 用在 |
|---|---|---|
| `positional` | `值` | `resize a.png` |
| `option` | `--width 800` | 大部分具名參數 |
| `switch` | 有就 `--force`，沒有就不放 | boolean |

`stdin` 指名從標準輸入送入的參數；`null` 表示不使用 stdin。`source` 保存產生 spec 時的檔案
指紋，用於判斷內容是否過期。草案規定過期時只標記、不自動重產，因為重新描述需要 LLM，
應由上層明確決定。

正式 `tooljson` 延續「schema 之外還要描述 execution」這個問題，但已採用不同資料形狀；
實作者不得從本頁複製舊 JSON 當新 spec。
