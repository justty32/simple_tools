# Live Agent Machine 頭腦風暴報告

日期：2026-08-11

## 結論

目前 coding agent 的真正輸入早已不只是 prompt，而是「prompt 指向的一個持續變動世界」：
workspace、工具、文件、設定、記憶、測試與人在 Round 中途做的修改共同決定求值。從這個事實
出發，Linux 不只承載 editor；它的 namespace、檔案、process、mount 與 event，已構成我們
編輯 agent program 的底層介面。

不過，直接說「資料夾就是 Lisp 函數」仍少了工程語意。較完整的重述是：

> Agent Machine 是一台對 **versioned world form** 持續求值的慢速機率 Lisp machine；
> Linux/VFS 是它可組合的 image editor 與 effect substrate；每個 Step 固定讀一個 view，
> 每次修改經 typed intent、generation check 與 settlement 後，才成為下一次求值可見的新定義。

這個模型帶來三個最重要的新結論：

1. **agent program 的單位是 live world，不是單檔，也不是 prompt。** prompt 比較像進入點；
   workspace tree、namespace、capabilities、memory 與 goal 才構成 closure。
2. **Agent Machine 管理的不只 CPU 類資源。** 它同時調度金錢／token、assurance、人的注意力與
   可介入性，在成本、風險與人類認知之間選擇一條可接受路徑。
3. **未來 editor 不會只是文字框加檔案樹。** 它需要同時瀏覽 form、時間、因果、context、
   effect、權限、證據與分支；檔案瀏覽器只是其中一個 lens。

## 我想出的系統形狀

```text
人類／agent 編輯 world form
      │ source、mount、instruction、tool、memory、goal
      ▼
versioned logical image ── context compiler ──> Step manifest
      ▲                                      │
      │                                      ▼
typed commit <── validate / verify <── stochastic proposal
      │                                      │
      └──── Linux effect adapter <── tool intent

橫跨全程：cost ledger · assurance graph · human intervention network
```

Linux 檔案系統仍是最小、通用、可 script 的表面；Agent Machine event journal 才是權威語意；
image workbench 則把兩者投影成人能理解並介入的介面。三者不是競爭關係。

## 與現有設計的關係

這不是另起爐灶，而是把既有材料接起來：

- [Agent Machine](../../freepy/future/agent-machine/README.md) 已定義機率 ALU、logical image、proposal、
  verifier、ledger 與 condition/restart。
- [Linux-as-Lisp](../../freepy/future/agentfs/LINUX-AS-LISP.md) 已把 path、namespace、mount、handle、process
  對應到 symbol、environment、binding、reference 與 evaluator。
- [Agent World](../../design/agent-world/README.md) 已定義 per-actor view、context compiler、memory graph、
  Step／Round／tick 與 typed file protocol。
- [agentloop](../../../freepy/agentloop/ROUNDS.md) 已實作 Round 中追加 instruction/tool 與安全邊界，
  是 live redefinition 的第一個可運作雛形。
- [Lisp 固化報告](../../research/languages/janet/README.md) 與
  [C++ 固化報告](../../research/languages/cpp/README.md) 已給出「Python 探索、fixture 固化、語意核心與
  effect adapter 分離」的方法。
- [`plan_shell.py`](../../../freepy/prototypes/README.md) 與
  [`tooljson`](../../../freepy/llmkit/tooljson/README.md) 已示範從機率探索萃取成 typed、跨語言契約。

新的部分是把 **workspace edit 正式視為 image redefinition**，再把 assurance、人類介入與
編輯器 UX 放進同一個模型。

## 閱讀順序

瀏覽器導覽入口是 [index.html](index.html)；以下 Markdown 是唯一正文。

1. [世界作為函數](01-world-function.md)：命題、形式化與 filesystem form。
2. [持續求值與版本語意](02-live-evaluation.md)：Round、Step、安全邊界、fork 與 replay。
3. [Assurance、成本與可互動性](03-resource-economy.md)：三者不是同一種資源。
4. [千萬 agent 的 assurance 網路](04-assurance-network.md)：傳播、污染與人類穩定節點。
5. [超越檔案瀏覽器的 Image Workbench](05-image-workbench.md)：未來編輯器與除錯器。
6. [系統架構與固化飛輪](06-system-architecture.md)：如何接回 FreePy、Janet、C++ 與 Linux。
7. [十二個新方向](07-new-directions.md)：產品、工具與研究假說。
8. [風險、反證與最小實驗](08-risks-experiments.md)：如何證偽，而不只把隱喻寫漂亮。

## 報告邊界

這是跨元件研究報告，不是已定案的 implementation spec。現行 Round／Step 語意仍以
[`agentloop/ROUNDS.md`](../../../freepy/agentloop/ROUNDS.md) 為唯一真源；實際優先序只看
[`ROADMAP.md`](../../../freepy/ROADMAP.md)。[`Agent Machine PLAN`](../../freepy/future/agent-machine/PLAN.md)
保存的是延後候選里程碑，不代表現在要做。

尤其是「agent loop 是否是一種以自省為運算本質、不同於 Lisp 的函數」目前只保留觀察，
本報告不急著定義。它需要另外研究 recurrence、history feedback、reflection 與 evaluator 的
差異，不能靠一次類比下結論。
