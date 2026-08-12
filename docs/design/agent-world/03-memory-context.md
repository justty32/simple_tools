# 組織化記憶與 context 編譯

## 不是刪 history，而是建立不同表示

系統至少保存三層：

1. **Source trace**：完整且不可變的 user、assistant、tool、event 紀錄。
2. **Organized memory**：由 trace span、檔案或 agent 產物提升出的 objects、facts、relations 與 indexes。
3. **Context view**：某個 Step 真正送入 `ask()` 的有限 working set。

所以「把上下文段落摘成檔案」是：全文進 immutable object，model input 的縮減副本改放 link card。原始 trace 不動，tool call/result pairing 也不動。

```text
trace span ──promote──> object(hash)
   │                      ├─ summary/view
   │                      ├─ provenance
   │                      └─ facts/links
   └──────── manifest 留 source + ref ──> Step context
```

## promotion 要保存兩樣東西

一個 6 KB 調查段落被提升後：

```markdown
[memory:m42 · ASF sandbox 架構 · 6.1 KB · round-18/s3]
摘要：host supervisor 配合 OS sandbox；agentfs 本身不是隔離層。
需要證據或實作細節時載入：[全文](memory:m42)
```

object 保存原 bytes；link card/summary 是可重建 view。metadata 至少有 source span、role、owner instance、Round/Step、hash、mime、classification、readers、kind、links、summary producer 與 supersedes。

摘要錯了不改正文。修訂摘要產生新 view/version；stable alias 可移向新版，舊 content ref 永遠仍指原 object。

## 記憶是 evidence graph，不只是資料夾

吸收 `handy`「意圖也是更長的推論鏈」後，建議分五層：

| 層 | 內容 | 能否作為確定性推論前提 |
|---|---|---|
| evidence | 原文、tool output、檔案 hash | 可以，但仍要看來源信任 |
| mechanical facts | parser/runtime 可重現抽出的事實 | 可以 |
| hypotheses | LLM 對角色、意圖、關係的猜測 | 不可以 |
| accepted grounding | 經人工／測試／證書接受的假設 | 可以，須帶版本與範圍 |
| conclusions | 規則從 facts 推出的結果 | 可以，且記 dependency set |

例如模型猜「變數 `a` 是錯誤碼」先是 hypothesis；人確認或測試證實後成 accepted grounding；推論器才能由此推出「這段在結構化記錄錯誤」。

一條 grounding 被撤回時，依 dependency edges 讓所有下游 conclusion 失效，再編譯受影響的 memory views。不能只改摘要文字，留下陳舊推論繼續被 recall。

## 多個目錄是多個 view

```text
/self/memory/
  objects/<sha256>          immutable content
  episodic/<round-id>/       事件／時間 view
  decisions/<topic>/        決策 view
  facts/<entity>/           fact heads
  procedures/<name>/        工作方法
  tasks/<task-id>/          task scope
  topics/<tag>/             索引 view
  inbox/                    收到但未整理
```

同一 object 可以同時出現在 task、topic、decision；目錄只是 ref/mount。真正關係還包括 chronology、provenance、`supports`、`contradicts`、`depends-on`、`supersedes`，不該硬塞成唯一樹狀分類。

第一版先做 headings、tags、backlinks、全文搜尋；embedding 只是未來的 candidate provider，結果仍要經 ACL 和 bytes/token budget，不能直接把相似內容灌進 prompt。

## Context manifest 是可重播的真相

每個 Step 保存實際 model input 的順序：

```json
{
  "round": "round-23",
  "step": 4,
  "blocks": [
    {"source": "system:policy", "view": "full", "pin": true},
    {"source": "user:latest", "view": "full", "pin": true},
    {"source": "memory:m42", "view": "card"},
    {"source": "memory:t81", "view": "section", "fragment": "#limits"}
  ]
}
```

manifest 自己也 content-addressed，與 model response、tool calls 一起記錄。它回答「模型當時真正看見什麼」，而不是「資料庫當時有哪些東西」。

### LOD：同一記憶的載入層級

可借 OS-as-game 的 LOD 概念，但必須顯式：

```text
card -> summary -> section -> full object -> source evidence
```

這些是從同一 ref 派生的 view，不是五份互相漂移的正文。模型可先列 catalog/card，再逐層 dereference；每次 load 固定 depth=1，避免 link bomb。

## Context compiler

```text
compile(effective instructions, task, Round state,
        requested refs, candidates, model limit, budget)
    -> immutable manifest + model messages
```

建議優先序：

1. system policy、當前 user/additional instructions 永遠 inline。
2. 未閉合的 assistant/tool pairing 永遠 inline。
3. 明確 pin 或本步 `memory_load` 的 refs。
4. 近期、task-scoped、依賴圖直接需要的 working memory。
5. 其餘相關項只放 card；無關項保持 cold。

compiler 像 pager，不是任意摘要器。要有 hysteresis 防止每步反覆 load/unload，並記錄每個 block 被 inline/card/exclude 的原因、token estimate 和 resolver generation。

若 relevance ranking 使用 LLM，compiler 的該階段要標 `nondeterministic` 並保存輸入、輸出和證書；較安全的 v1 先用明確 pin、recency、task link 與規則排序。

## 活躍態與沉寂態是同一個本體

agent 沉寂時，memory objects 和 aliases 留在持久層；agent 活躍時，context 只是把其中一小部分 mount 成 working set。兩態不是兩套記憶，不需要啟動時把「整個我」塞回 prompt。

agent 結束後，task-owned report/facts 可存續；instance-private scratch 依 retention 回收。transcript、Step manifest、task report、stable alias 與 pin 是 GC roots。

## 權限與 authority

- 分享 link 不等於授權；list/search/load 都在 resolver 檢查 actor。
- 無權者不能看到 title、hash、path 或「存在但 denied」的側通道。
- source 含 secret 時，object 與 derived metadata 只能同等或更嚴格。
- 載入的舊 user message只是「過去曾說過」，不自動成為本 Round 新指令。
- web/tool/peer memory 一律是 untrusted data；只有真實 supervisor/user event 可建立 authority block。

這個 authority 必須是資料模型欄位，不可只靠 Markdown 警語。
