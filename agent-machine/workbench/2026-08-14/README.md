# 2026-08-14 工作台收尾

本目錄保存這一回合的設計決定、可執行原型與故障證據，不是正式 AOS ABI。後續由使用者親手開始 C++ 實作；開工入口見 [`../../START-HERE.md`](../../START-HERE.md)。

## 收尾結論

- AOS 的核心比喻是「Function 像 CPU 指令，filesystem 像記憶體」。Linux process 是 leaf Function 的通用形式，不是所有 Function 的定義。
- 使用者稱正在執行的 Function 實例為 Task；Task 不是 PID。工作台暫用固定 Task ID 貫穿保存紀錄，每次 process 啟動只是其中一次 attempt（嘗試）。
- 沒有可信完整結果時必須停在 unknown／repair，不可因重開而自動再送可能已有副作用的工作。
- parent／child relation、Task events 與 Task Receipt 由 Runtime authority 寫入；process adapter 只產生原始執行證據。
- Agent root 的實際絕對 path 是它的 ID 與地盤；普通忙碌呼叫拒絕且不建 Task，追加與排隊必須明確選擇。

產品方向與原型暫選的完整分界見[決定表](round-2-decision/01-DECISIONS.md)與 [`full/00`](../../full/00-STATUS-AND-SOURCES.md)。

## 原型證據

| 切片 | 已證明的窄範圍 | 不可外推 |
|---|---|---|
| [P0 Python](p0-function-python/README.md) | no-shell argv、raw streams、termination、部分 unknown 不重跑 | Task Receipt、scheduler、正式 ABI |
| [P0 C++23](p0-function-cpp/README.md) | `fork/execve`、nonblocking pipes、`poll`、exit／signal／launch error | durable writer、timeout、正式 schema |
| [P0 Janet](p0-function-janet/README.md) | 純資料 policy 與建議 | Linux process、正式提交權 |
| [P1a-1 v2](p1a-task-tree-python-v2/README.md) | Task-local Call、parent relation、fake child recovery | root acceptance、真 process |
| P1a-2 Phase 1 | root registry、accept／recover 與重開穩定性 | child relation、真 process |
| P1a-2A | root → first → second 的兩個 fake child、保存順序與中斷恢復 | process attempt、Agent、scheduler |
| [P1a-2 Phase 2](p1a2-process-python/README.md) | 第一個 child 真正啟動一次 process、唯讀 binder、Receipt commit、完整證據補提交、不完整證據不重跑、主要矛盾停止 | 完整 crash matrix、正式 C++ Runtime |

## Phase 2 停止位置

P1a-2 Phase 2 的**本輪窄切片已結束**。目前工作台全套是 **74/74 tests**，其中 16 項直接涵蓋 process 路徑；精確命令、測試名稱與結果以 [P1a-2 工作台 README](p1a2-process-python/README.md)為準。

目前流程是：

```text
root Task
  first -> process leaf -> attempt-1 -> 一個 P0 invocation
  second -> deterministic fake leaf
```

已驗到 `dispatch_intent` 先於 process effect；完整 raw evidence 可在不增加 P0 UUID 與副作用次數的前提下補出相同 Receipt；after-spawn 中斷留下不完整證據時停在 repair；request／stream／UUID／目錄形狀矛盾則視為 corruption，不能用 repair 洗掉。非法的 `accepted → dispatch_intent → repair_required(generation)` 歷史也會被拒絕，因為 generation repair 必須發生在 dispatch intent 之前。

仍未完整驗證：

- P0 三個 hook 的完整 failpoint 矩陣。
- 十段 root failpoint 的 Phase 2 串接。
- 已知的 nonzero、signal 與 spawn error 經 leaf Receipt 到 parent observation 的完整案例。
- process golden bytes 與完整跨實作差分。
- producer-side bounded allocation；現有 Python P0 `communicate()` 仍可能先配置超過 binder 上限的資料。
- live process 重接、舊 worker fencing、斷電、多 writer、NFS、TOCTOU、正式 scheduler、Agent 與 common Return ABI。

所以「本輪 Phase 2 結束」只表示這個窄切片已到停止線，不表示 [`07-P1A2-PROCESS-SEAM.md`](round-2-decision/07-P1A2-PROCESS-SEAM.md) 的所有候選測試或完整 AOS 已完成。

## 重跑方式

從 repository root 執行：

```sh
cd agent-machine/workbench/2026-08-14/p1a2-process-python
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v
```

測試 helper 會在 Linux `/tmp` 建立 store。Windows 上應從 WSL 執行；source 可位於 `/mnt/c`，但 store 與 filesystem crash tests 不可放在 `/mnt/c`。

## 交給 C++ 的邊界

Python 在這裡是**可執行參考模型**，不是 C++ ABI 或架構。第一個 C++ 落點是 process evidence producer：管理 argv、raw streams、fd、signal、process lifecycle、硬上限與原始證據；先保留 Python Runtime／binder 作 oracle，使用同一批 fixtures 做差分。

C++ 第一片不要同時取得 Task relation、events 或 Receipt 的寫入權，也不要順便加入 Agent、Git、Janet、跨機或完整 scheduler。等 process evidence 在 success、known failure、unknown 與 corruption 上都能穩定對齊，再決定下一個 durable seam。

## 文件地圖

- [下一輪決策包](round-2-decision/README.md)：使用者方向、暫選與詳細工作契約。
- [Function／Task authority](round-2-decision/06-FUNCTION-TASK-MODEL.md)：relation、accept、recover 與 single-writer 規則。
- [Process seam](round-2-decision/07-P1A2-PROCESS-SEAM.md)：attempt、P0 evidence、binder 與 Receipt 邊界。
- [Exact formats](round-2-decision/08-P1A2-FORMATS.md)：只供工作台互通，不是產品 schema。
- [完整設計](../../full/README.md)：Function 到 Agent 的長期主線。
- [繁中濃縮包](../../wait_user/README.md)：核心模型、恢復、排程與 options 的短讀版。
