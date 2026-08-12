# 風險、反證與最小實驗

## 最需要防的八個誤解

| 誤解 | 反例／後果 | 修正 |
|---|---|---|
| 任意資料夾天然是函數 | cache、圖片、secret、prompt injection 沒有共同求值語意 | WFIR 保留 unknown/data，effect 需 typed intent |
| live disk 就是 immutable image | partial write、symlink、外部 process、crash 會混合世代 | snapshot/generation + observe/settle |
| 更多 agent 等於更確定 | 同模型、同資料的錯誤高度相關 | independence tags、異質 evidence、mechanical verifier |
| 人工 review 一定提高品質 | notification fatigue 會導致機械 approve | sparse risk gates、attention ledger、抽樣校準 |
| path 或 mount 就是權限 | 名稱可猜、projection 可偽裝、sandbox 可缺失 | actor-bound handle + server check + OS enforcement |
| patch 套得上就是仍符合意圖 | 新 world 中語意可能已變 | conflict condition、replan/verifier，不靜默 rebase |
| UI 顯示綠燈就是 machine truth | cache/stale projection 可能過期 | generation-bound read model、可追 event/evidence |
| Lisp 隱喻本身就是實作 | filesystem 不是 homoiconic AST，Janet backend 也未全證明 | contract/fixture/differential gates |

## 八個可證偽實驗

### E0：Versioned workspace reducer

以 in-memory tree 實作 snapshot、overlay、generation CAS、`WorkspaceIntent/Settlement`。兩個 Round
從同一 generation 寫同檔，至多一個 commit；另一個得到 typed conflict。event replay 必須重建
相同 tree hash。

### E1：In-flight live edit

scripted model 在 Step 1 綁 `workspace@g7` 時，模擬人存檔成 g8。驗證 Step 1 manifest 永遠仍指
g7，下一 boundary 才能選 g8；agent 的 stale write 不可覆蓋人類版本。

### E2：Effect crash matrix

在 intent 落盤前、落盤後未執行、effect 完成未 settlement、settlement 後未 snapshot 四點注入
crash。恢復必須分辨 safe retry、unknown/reconcile 與已 commit，不能重做副作用。

### E3：Assurance frontier simulator

建立 patch-only、patch+tests、同模型投票、異質 critic、human gate 五種 plan，注入成本、deadline
與 reviewer queue。同模型票數不得假裝獨立；test failure 仍 charge；human slot 滿時只能等待、
降級明示或 abort。

### E4：Assurance propagation at scale

先生成十萬 agents／百萬 claim edges，再逐步放大到千萬 dormant agent records。測試上游 evidence
撤回的精確 invalidation、cycle detection、correlation cluster 與 bounded active working set；
不能每次變更全圖掃描，也不能因 cache eviction 改變 correctness。

### E5：Human stability routing

用相同任務比較「每步 approval」與「只在高 blast-radius condition 介入」。量 review 次數、
decision time、錯誤攔截、機械 approve 比率與下游解鎖數。若 sparse routing 沒降低負荷且保持
風險，假說就不成立。

### E6：Read-only Image Workbench

先用 fake events 產 timeline、manifest、diff、assurance 與 namespace lenses。受試者須在限定時間
回答：模型看了哪版、哪個 effect 未知、哪個 claim stale、能安全 invoke 哪個 restart。與純
transcript + file tree 比較正確率與時間。

### E7：Spec crystallization

選 `tooljson` 的 15–20 個 pure/exec fixtures，跑 Python/Janet differential；再以 dcap 產一個
C++ leaf，只在 Linux 執行 effect。pure transition 必須完全一致，平台未支援時 fail closed，
否則「Lisp core + native adapter」仍只是偏好。

## 建議實作切片

```text
M0  Fake WorkspaceSnapshot + reducer + event replay
M1  StepManifest 加 workspace/policy generation
M2  edit/test 的 typed intent + overlay + settlement
M3  assurance case + cost ledger + human condition simulator
M4  read-only Workbench：timeline/context/diff/evidence/namespace
M5  multi-agent overlays + assurance graph scale simulation
M6  Python/Janet differential solidification
M7  有證據後才做 control UI、9P/FUSE 與 native adapters
```

這個順序不取代既有 Agent Machine M0/M1。最合理的做法是先把 Workspace records 與 simulator
放進同一批純核心研究 fixtures，等既定資源守恆、late result、false completion、verifier gate
通過，再擴大 image redefinition。

## 待決策問題

1. `WorkspaceRef` 指向 Git tree、content-addressed Merkle tree，還是 transaction store generation？
2. 外部 editor 直接存檔時，是自動建立新 generation，還是先形成 uncommitted delta？
3. 一個 Round 預設追最新 workspace，還是 pin branch，只有明確 restart 才 rebase？
4. WFIR 是顯式 `.agent/form.json`、compiler-derived view，還是兩者疊合？
5. 哪些 claim 必須 mechanical verifier，哪些可由 human authority 接受？
6. assurance debt 的 owner、deadline、繼承與撤回 policy 是什麼？
7. human intervention node 如何授權、避免利益衝突並衡量 reviewer reliability？
8. Workbench 的最小共同 protocol 是 event/query API、agentfs path，還是兩者？
9. history feedback 是否真構成不同於 Lisp 的自省求值？此題刻意延後，不阻塞 M0–M4。

## 何時算這個方向被證明

不是做出一張漂亮 graph，而是同時出現以下證據：

- 每個 Step 可指出確切 world/context generation；
- 人與 agent 並行修改不產生 silent lost update；
- crash/replay 不重做未知 effect；
- Goal 只有合格 evidence 才 achieved；
- assurance 撤回能使正確下游失效；
- human routing 在真任務中降低注意力成本而未增加漏錯；
- Workbench 比 transcript/file tree 更快找出 causal failure；
- 第二語言實作能用同 fixtures 重現核心 transition。

若其中任一項無法成立，就應縮小「Linux 是 Lisp image／editor」的主張，而不是靠更多隱喻
補洞。
