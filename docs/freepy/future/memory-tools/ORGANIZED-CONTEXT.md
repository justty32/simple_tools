# 組織化記憶與 context filesystem

這是 [memory_tools 規劃](PLAN.md) 的後續層。第一版只無損卸載大型 tool result；這份描述更一般的未來模型：模型上下文中的舊段落可以提升成記憶檔案，下一步只保留小型摘要與可載入連結。

## 核心原則

```text
source trace 不可變
memory object 可尋址
model context 是按需編譯的 working set
```

不能直接裁掉真正的 history。系統至少維持三層：

1. **Source trace**：user、assistant、tool、事件的完整不可變紀錄，供 audit、重播和 tool-call 配對。
2. **Organized memory**：從 trace span、workspace 或 agent 產物提升出的不可變 objects、索引與關係。
3. **Context view**：下一次 `ask()` 真正傳給模型的有序 blocks；部分 inline，部分只放 link card。

因此「摘出段落」實際上是改寫 context view，不是破壞原始對話。現有 API 若要求傳完整 message history，可由 context compiler 產生等價的縮減副本。

## 提升與連結

例如原 context 有一段 6 KB 的 sandbox 調查。提升後 object 保存全文，工作集只留：

```markdown
[memory:m42 · ASF sandbox 架構 · 6.1 KB · source round-18/s3]
摘要：以 host supervisor 配合 OS sandbox；不是把 agent runtime 本身做成容器。
需要實作邊界或證據時再載入：[全文](memory:m42)
```

提升操作輸入必須是明確 span，不靠模型重貼內容：

```text
memory_promote(
  source={round: "round-18", message: "msg-7", start: 120, end: 6340},
  kind="research",
  title="ASF sandbox 架構",
  summary="..."
)
```

object metadata 至少包含 content hash、原始 span、role、author/agent instance、round/step、mime、bytes、建立時間、classification、readers、kind、links 與 supersedes。summary 是可替換索引，不是原文；錯了可以重建，hash object 不變。

## Context manifest

每次 Step 都保存一份 manifest，精確描述送進 `ask()` 的內容和順序：

```json
{
  "round": "round-23",
  "step": 4,
  "blocks": [
    {"type": "inline", "source": "system:policy", "pin": true},
    {"type": "inline", "source": "user:latest", "pin": true},
    {"type": "ref", "target": "memory:m42", "view": "link-card"},
    {"type": "ref", "target": "memory:t81", "view": "full"}
  ]
}
```

manifest 自身 content-addressed，與 response、tool calls 一起記錄。如此可回答「模型當時究竟看見什麼」，也能重建某一 Step，而不是只知道資料庫裡理論上有哪些記憶。

block 可有四種保留政策：

- `pin`：必須 inline；system policy、當前 user 指令、未完成 tool-call pairing 屬此類。
- `working`：近期且相關，預設 inline。
- `linked`：只留 link card，可按需 load。
- `cold`：不主動出現在 context，但能由 catalog/search 找回。

當前 Round 的使用者指令不可因 compact 被藏到 link 後面；additional instruction 也至少 pin 到 Round 結束。被載入的舊記憶不能變成新的 system/user authority。

## 檔案視圖

同一 object 可出現在多個組織視圖，不複製 bytes：

```text
/self/memory/
  objects/<sha256>                 immutable bytes
  episodic/<round-id>/              依事件來源瀏覽
  decisions/<topic>/               決策與理由
  facts/<entity>/                  可更新認知的 versioned heads
  procedures/<name>/               工作方法
  tasks/<task-id>/                 task-scoped memory
  topics/<tag>/                    索引 view
  inbox/                           他人分享、尚未整理

/self/rounds/<round-id>/steps/<n>/
  context.json                     manifest
  response                         model message
  refs/                            本步引用的 memory links
```

`decisions/`、`topics/` 等是目錄索引／mount view；真正內容仍在 object store。分類可多值，移動分類不改 object address。stable alias 指向一個 versioned head；舊版本保持可讀並以 `superseded-by` 連接，避免連結腐爛。

## Context compiler

compiler 的輸入是 effective instructions、當前 task、最新 Round state、候選 memory、模型 context limit 和 token budget；輸出是 manifest 與 model messages。

建議順序：

1. 放入不可卸載的 policy 與本 Round 指令。
2. 保持尚未閉合的 assistant/tool pairing。
3. 加入明確 pinned 或剛由 `memory_load` 要求的 refs。
4. 依 task、recency、link graph、kind 和 estimated tokens 選 working set。
5. 其餘相關項目只放短 link card；不相關項目保持 cold。
6. 記錄每個 block 被 inline、link 或排除的理由與估算成本。

它像 pager，不是任意摘要器。需有 hysteresis，避免同一段每步 unload/load 抖動；也要限制自動載入的單檔、總 bytes、深度和 refs 數。

## 組織不是只有資料夾

記憶同時需要四種關係：

- hierarchy：task、topic、entity 的可瀏覽目錄。
- chronology：從 Round／Step 找回當時事件。
- provenance：從摘要回到原始 span、工具與產生者。
- semantic links：`supports`、`contradicts`、`supersedes`、`depends-on`。

第一版不用向量資料庫。先做 catalog、kind、tags、backlinks、全文搜尋和 heading-level load；這些較容易解釋、測試與授權。embedding recall 可作為另一個 candidate provider，不能繞過 resolver 或直接把結果塞入 prompt。

## 跨 agent 與權限

- memory owner 和 readers 來自 source 的有效權限；提升不能降低 classification。
- 分享 link 不等於授權。接收者 dereference 時仍檢查 actor、instance 與 grant generation。
- 對無權讀取者，list/search 也不能洩漏標題、路徑、hash 或存在性。
- parent 可整理 child 交付的 report，不自動取得 child 私有 scratch memory。
- agent 結束後，task-owned memory 可存續；instance-private working memory 依 retention policy 回收。
- secret 若意外進入 source，只能進同等或更嚴格的 object；自動摘要不得把 secret 洩到低權限 metadata。

## 指令注入與信任

memory load 回來的內容一律包在有 provenance 的 data block，標示原始 role、時間、owner、trust 與是否已過期。它不能因內容寫著「忽略先前指令」便取得新的權限。

外部網頁、tool output、peer memory 都是 untrusted data。只有 supervisor 能建立 system policy block；只有實際 user event 能建立本 Round 的 user-authority block。這個區分必須存在於資料模型，不能只靠 Markdown 警語。

## 生命週期與垃圾回收

- object immutable；修訂產生新 object 與 `supersedes` edge。
- transcript、manifest、task report、stable alias 和 pin 都是 GC roots。
- 移除 alias 不等於立即刪 object；先做 retention window 和 reachability scan。
- 刪除依法必須可做時，留下 tombstone 與審計資料，但不可保留已要求刪除的正文。
- broken ref 回明確 tombstone／denied／missing，不靜默改抓「相似的新內容」。

## 分期

1. 保持現有 tool-result unload/load，補 source span 與 immutable manifest。
2. 將 assistant 的長篇 research/report 允許人工 `promote`；user 指令仍不卸載。
3. 加 catalog、backlinks、versioned alias 與 agentfs read-only view。
4. 引入 context compiler，但只自動 link 已經 promote 的內容。
5. 有離線 replay/eval 後，才允許自動選 span、產摘要和 semantic recall。

必測：重建同一 Step 得到相同 manifest；原始 trace 不變；tool pairing 不破壞；current instruction 永遠 inline；summary 錯誤不污染 source；ACL 作用於 list/search/load；alias 更新不破舊 ref；link bomb 和 context thrashing 有界；untrusted memory 永不提升 authority。
