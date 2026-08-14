# 真正待驗方案

> **工作稿、非正式 AOS ABI。** 本頁只列仍開放的選擇。P1a-2 固定目標見 [`06-FUNCTION-TASK-MODEL.md`](06-FUNCTION-TASK-MODEL.md)；固定目標不等於已實作或正式 ABI。

## 不再是 P1 開放題

**P1 驗證證據：**Call不含 relation、每 Task自存 Call、parent events可保存relation。**P1 暫選：**`call_ref{sha256,size}`作本片identity、top-level Task registry、planned才freeze、unknown無Receipt與B4規則。這些不是正式AOS既定介面。

## 1. Call resolver 與長期保存

P1 的 task-local `call.json` 是 resolver/location policy，不是 identity。長期可選：

| 方案 | 優點 | 風險 |
|---|---|---|
| 維持每 Task 一份 Call | Task 自足、檢查直接 | 重複 bytes、retention 隨 Task 綁定 |
| content-addressed store（CAS） | 去重、可共享 | GC root、權限、損壞域與生命期更複雜 |

P1a-2固定`call_ref{sha256,size}`且容許相同Call建立不同Tasks；正式AOS仍須決定是否沿用。P1a-2不做CAS。

Call 引用的 filesystem memory 可另選 live、checked manifest 或 snapshot。P1a-2 只對 fixture executable 釘 absolute path＋content SHA-256；不 snapshot dependency 或 directory tree。

## 2. Task ID allocation 與 public accept

P1 fixture 可由 versioned `parent_id + slot` 算 deterministic child ID，證明 recovery 不另生身分；正式系統仍可比較 random/monotonic ID、call-hash＋occurrence 或 parent＋stable slot。

P1 fixture讓ID獨立於Call identity；正式系統仍待選。P1也不承諾public `accept` idempotency；產品若需要重送安全，須另定request key、期限與collision policy。

## 3. Common Function Return ABI

Function Return 與 Task Receipt 已分開，但 caller-facing Return 仍待選：

| 方案 | 優點 | 風險 |
|---|---|---|
| integer logical status＋stdout/stderr | 接近 shell/C | signal、launch error 需額外欄位 |
| tagged `ok/error/unknown`＋leaf termination evidence | 不丟底層分類 | 介面較厚；unknown 又不能是假 Receipt |

P1a-2 end-to-end parent success oracle只走 `exited(0)`、預期 stdout、empty stderr；binder仍須把 nonzero、signal、spawn_error保存為完整已知 leaf Returns並可建 Receipt。其 composite mapping不能由此 fixture推成 common ABI。

## 4. Unknown lifecycle 與 repair

P1 暫用 `repair_required` 並確定 unknown 無 Receipt、不自動 retry。仍待選：

- 長期是獨立狀態、`paused(reason=unknown)`，或不可逆 terminal unknown marker。
- repair 如何用外部證據標記已完成，或明確建立**新 Task**重做；不得原地重送舊 Task。
- composite 自身 effect unknown 如何表達。P1a-1 validator 的「parent repair 必有 child repair」不應提升成通則。

## 5. `aos exec` 與 callable directory

AOS以filesystem path承載Definition是已定方向；CLI是否搜尋PATH、何時解析成target與surface仍待P1c：

```sh
aos exec ./ask "do something"
aos exec . "do something"
ask "do something"       # shell/proxy PATH sugar
```

P1候選做法是先把PATH sugar解析成明確path；這還不是正式規則。directory module的固定`./call`、manifest、façade或是否允許慣例入口仍待比較。

## 6. Step／Round 的 Agent 語意

使用者已定 Step、Round 都是 Function，且 Step 有原子角色。P1b 比較：

| 方案 | 解讀 | 代價 |
|---|---|---|
| Step 無 AOS-visible children | 只在前後 pause/interleave | 較重操作需提升為 Tool/Round |
| Step 可含 children、整段一次發布 | 封裝較強 | 中途觀察與 recovery 說明較難 |

兩者都不保證 external rollback。

## 7. P1c portability 與 P2 Runtime

P1c 才驗 agent root capacity/queue、portable semantic history、sealed image、checkpoint transaction、Git、clone detached 與 attach。stable checkpoint 不得帶 local claim、pending/running Call 或 unresolved unknown。

P2 比較 on-demand／resident manager、integrated／separate executor、central store 與 derived indexes。中央 Runtime 與 recover-before-ready 是已定方向，不重新投票；P1 的 per-task full replay 不是可擴充答案。

## 8. 明確延後

multi-writer、network AOS、NFS、secure sandbox/TOCTOU、symlink/rename/path reuse、secrets、streaming、large-output retention、GC 實作與 C++ writer 都不進 P1a-2。process seam 的窄契約見 [`07-P1A2-PROCESS-SEAM.md`](07-P1A2-PROCESS-SEAM.md)。
