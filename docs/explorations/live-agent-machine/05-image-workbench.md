# 超越檔案瀏覽器的 Image Workbench

## 單檔 editor 為何不夠

agent machine program 同時分布在 source、目錄結構、tool specs、policy、goal、memory、context
manifest、event trace、workspace generation 與 evidence graph。普通 editor 擅長一份文字的局部
修改；側邊欄檔案樹補上空間導航，仍看不到：

- 哪個 Step 在哪個 generation 看過哪些內容；
- 一次模型 proposal 經過哪些 policy、effect 與 verifier；
- 某個結論依賴哪些上游 evidence，是否已過期；
- agent 實際能看到／寫到什麼，而不是設定宣稱什麼；
- 人此刻介入會影響哪個 continuation，能選哪些 restart。

Lisp 單檔或 REPL 能靠統一 form 與 image inspector 撐得更久；但大量 agent 的時間、並行與
authority 仍超出純文字 buffer。需要的不是丟掉 Linux，而是在它上面建立多種同步 lens。

## 核心介面：focus + lenses

Workbench 每次聚焦一個 object：agent、goal、Round、Step、claim、effect、workspace generation
或檔案。周圍的 lens 都跟著切換：

| Lens | 回答的問題 |
|---|---|
| **World/Form** | 這次求值的 inputs、mounts、tools、memory、postconditions 是什麼？ |
| **Source** | 哪些檔案／目錄構成 form，現在的 diff 是什麼？ |
| **Time/Cause** | 哪個 event 導致哪個 Step、effect、delta 與 decision？ |
| **Context** | 模型當時實際看見哪些 blocks，為何 inline/card/cold？ |
| **Effect** | proposal 如何變 intent，誰批准，執行與 settlement 結果為何？ |
| **Assurance** | claim 有哪些證據、缺口、相關來源與人類 acceptance？ |
| **Namespace** | 此 actor 的 `/work`、`/tools`、`/memory`、`/srv` 實際解析到哪裡？ |
| **Economy** | 花了多少 token、金錢、時間與人類注意力，下一個選項代價多少？ |

這些不是八套資料庫，而是同一 event/image 的 projection。使用者可從一行 diff 點到產生它的
tool intent，再點到模型當時的 context、測試 evidence 和批准者。

## 除錯器應是因果瀏覽器

「瀏覽一個 agent loop 的結果」不能只看 transcript。建議主畫面是可折疊 evaluation trace：

```text
Round R42 · goal G8 · workspace@g17
├─ Step 1 · manifest m1 · $0.03
│  ├─ proposal: edit a.py
│  ├─ intent: replace expected_hash=H1
│  └─ settlement: workspace@g18, delta d4
├─ Step 2 · manifest m2 sees g18
│  ├─ proposal: run tests
│  └─ evidence: 31 pass, 1 fail
├─ condition: assurance-insufficient
│  ├─ restart A: repair ($0.04)
│  └─ restart B: ask human (~2 min)
└─ human decision: narrow claim, choose repair
```

預設只顯示語意節點；展開才看 raw message、tool stdout、JSON event 或檔案 bytes。這種 semantic
zoom 借用 context 的 card → summary → section → full → source LOD，避免把人淹沒在 log 裡。

## 操作不以聊天框為中心

chat 仍適合表達開放意圖，但精確控制應採 **selection + typed verb**：

- 選 workspace generation：`fork`、`compare`、`make current`、`export snapshot`。
- 選 context block：`pin`、`lower LOD`、`show provenance`、`exclude next Step`。
- 選 effect：`approve`、`deny`、`reconcile`、`compensate`。
- 選 condition：`invoke restart`、`delegate`、`defer`、`abort`。
- 選 claim：`verify`、`narrow scope`、`request evidence`、`accept/reject`。
- 選 capability：`mount ro`、`revoke`、`delegate subset`。

每個 verb 顯示生效 boundary、scope、預估成本和可逆性，最後產生既有 typed operation；GUI 不
能另開一條繞過 Agent Machine 的 mutation 後門。

## 比檔案樹更好的幾種導航

### 1. Object browser

側邊欄預設不是宿主 directory tree，而是可切換的 synthetic views：By task、By agent、By
generation、By condition、By assurance gap、By human attention。需要時仍一鍵回原始檔案樹。

### 2. World map

用小型 graph 顯示 agent、goal、artifact、workspace branch 與 verifier 的高階關係。只畫目前
focus 的一至兩 hop，避免「千萬節點毛線球」；點節點再切入 trace 或 source。

### 3. Attention inbox

不是所有 notification，而是依 blast radius、不可逆性、deadline、缺失 evidence 與可解除的
下游阻塞排序的人類介入 queue。每張卡只要求一個 typed decision，附最小 evidence slice。

### 4. Counterfactual board

並排比較同一 snapshot 上不同 engine、context、tool policy 或 patch branch：成本、diff、tests、
assurance 缺口與尚未提交 effects。這比叫 agent 在同一 history 裡「再想一次」更可解釋。

### 5. Command/query palette

專家可直接查「所有使用 stale evidence 的 claims」或「持有 workspace writer lease 超過五分鐘
的 Rounds」，再批次呼叫受限 operation。這保留 shell 的組合力，不強迫所有操作走滑鼠。

## OS 仍是 editor 本體

Workbench 不取代檔案、pipe、Git、process 或 mount。任何 view 都能輸出穩定 path/ref；任何
重要事件都能回到可 script 的 typed CLI/API；介面掛掉不影響 machine truth。反過來，人在
普通 editor 存檔所造成的 delta 也會出現在 Workbench 的 time/effect lens。

這使「作業系統本身是編輯器」與「需要更好用的介面」同時成立：OS 提供 universal editable
substrate；Workbench 提供因果、時間、assurance 與人類注意力的認知座標。

## 最小可用 Workbench

第一版不做龐大 IDE，只做一個本地 read-only web view：

1. Round/Step/effect 的折疊 timeline；
2. 每 Step 的 context manifest 與 workspace generation；
3. diff + test/evidence links；
4. declared/effective capability 對照；
5. pending conditions 與 human queue。

等 projection 被真資料驗證，再加入 fork、restart、approve 等 control。這符合現有「先唯讀，
後 typed control；先 resolver，後 FUSE/9P」的路線。
