# memory_tools 規劃

**這份是實作規格，尚未寫程式。** 目標是把大型工具結果無損移出 context，需要時再載入；同一個 resolver 同時理解 JSON `$ref` 與 Markdown link。

模型上下文的一般化組織、段落提升、Step manifest 與 agentfs memory view 見 [組織化記憶與 context filesystem](ORGANIZED-CONTEXT.md)；那是建立在本文件無損 object/ref 核心上的後續層。

evidence／grounding／derived facts 與撤回失效機制見 [跨層設計報告](../../../design/agent-world/03-memory-context.md)。

## 核心語意

卸載不刪除 history message，只替換 `role="tool"` 的 `content`，保留 assistant `tool_calls` 與 tool message 的配對：

```markdown
[unloaded t7 · read_file("src/engine.py") · 12.4 KB · 第 3 步](memory:t7)
```

內容仍在 object store。載入時追加一則新內容到 history 尾端，標出原始回合／步與 tool call；不倒帶修改舊前綴。

只有 tool result 可卸載。使用者指令與 assistant message 是任務和決策脈絡，v1 不允許卸載。

## 一套 address，兩種結構

JSON 可以保存：

```json
{"$ref": "memory:t7"}
```

Markdown 可以保存：

```markdown
[parser 的完整輸出](memory:t7)
[設計決策](notes/design.md#網路邊界)
```

兩者最後都正規化成 `MemoryTarget`：

- `memory:t7`：session memory ref。
- `relative/file.json#/json/pointer`：workspace 內 JSON + JSON Pointer。
- `relative/file.md#標題`：workspace 內 Markdown；有 fragment 時只取該 heading section。

`memory_load` 接受 bare target、`{"$ref": ...}` 或完整 Markdown link 字串。HTTP(S)、`file:`、絕對路徑及越過 workspace/memory root 的 `..` 在 v1 全部拒絕。

## 工具

| 工具 | 用途 |
|---|---|
| `memory_candidates` | 列出可卸載 tool results 的短 ref、來源、步次與大小 |
| `memory_unload` | 批次卸載多個 ref，一次改 history 中段 |
| `memory_load` | 批次解析 ref／JSON `$ref`／Markdown link，追加內容 |
| `memory_links` | 只列出某份 JSON/Markdown 內可載入的連結，不展開正文 |

`memory_candidates` 解決模型不知道舊 tool message id 的問題。ref 在 tool result 寫入 history 時就由 session 配發，但平常不額外污染正文。

unload 必須批次：改一次歷史中段就會讓其後 prefix cache 失效；一次卸十則與分十次卸的快取成本差很多。

## 儲存格式

```text
<workspace>/.memory/
  objects/<sha256>          immutable 原始 bytes
  refs/<session>/<t7>.json  ref → object + metadata
```

metadata 至少包含 owner、readers、mime、bytes、hash、tool name/args、tool_call_id、原始回合與步、建立時間。對外 ref 短而好唸；對內 content hash 去重。

ref 不是權限 token。另一個 agent 從 communication message 收到 `[資料](memory:t7)` 後，loader 仍要檢查 reader grant；不可因 ref 難猜就當成授權。

## Markdown 載入規則

- 相對 link 以目前 Markdown 檔所在目錄解析，再確認仍在獲准 root。
- heading fragment 依正規化標題匹配，載到下一個同級／更高級 heading 前停止。
- `memory_links` 解析 Markdown links 與 JSON `$ref`，但不自動遞迴載入。
- v1 `memory_load` depth 固定為 1；模型要下一層必須再叫一次。
- 同一批 target 去重並檢測 cycle；總檔數、單檔與總 bytes 都有限制。
- code fence 裡看起來像 link 的文字不解析。
- 回傳內容包含 canonical source，避免模型不知道資料來自哪裡。

不自動遞迴是安全與 context 預算邊界：一個短 Markdown 可能連到整棵文件樹，不能一個工具呼叫就無上限展開。

## 自動卸載留到第二版

v1 由模型呼叫 candidates → batch unload → 需要時 load，先把可逆語意做對。自動政策所需
的 context limit 應由 endpoint metadata 取得，不塞進 preset。

自動卸載、手動載入可能更省，但在有實測前不加入隱藏行為。compact 若需要，是壓在 refs 之上的有損第二層，不取代 object store。

## 與回合、通訊的關係

metadata 同時記 `round_id` 與 `step_no`。回合內或跨回合載入都只是下一步的額外 context，不會把舊 Step 重新計算成新 Step。

communication transport 只傳文字 link；team tools 可授予某 agent 讀取一組 refs/paths 的權限。memory loader 是最後執行權限檢查的一層。

## 實作順序與測試

1. `address.py`：三種 target 與 Markdown/JSON decoder。
2. `store.py`：hash object、ref metadata、權限與原子寫入。
3. `markdown.py`：link、heading fragment、code fence。
4. `history.py`：candidate、批次替換、尾端載入。
5. `tools.py`：綁定指定 bot/session 後產生 dispatch。

必測：tool call 配對不破壞、同內容去重、批次 unload、JSON Pointer、Markdown heading、相對路徑越界、symlink、cycle、link bomb、未授權 ref、load 只 append 不改舊 message。
