# P1a-2 process / fake 工作台

這是用來做斷點恢復實驗的工作台筆記，不是規格，也**不是正式 AOS ABI**。背景與精確格式仍以相鄰的 [06](../round-2-decision/06-FUNCTION-TASK-MODEL.md)、[08](../round-2-decision/08-P1A2-FORMATS.md)、[09](../round-2-decision/09-P1A2-COMPOSITE-FAKE.md)、[10](../round-2-decision/10-P1A2-COMPOSITE-FAKE-FORMATS.md) 為準。

## 這個原型想證明什麼

- **Phase 1**：接受一個 `sequence_two` root 時，先把 Call（輸入描述）存好，再依序登記 root、補齊 `task.json`、`accepted` 與 `root_linked`。在這條狹窄路徑中斷後，恢復可只靠已存資料接續，並回到相同的持久檔案 bytes。
- **Phase 2A**：`sequence_two_fake` 的 root 可從已凍結的 root Call 長出兩個假 child：`first`、`second`。兩者都只是固定回傳值，不啟動程式；first 的結果符合成功條件後，才會做 second，最後才做 root 的合成 Receipt（完成收據）。這補的是兩層 Task tree 的恢復證據，沒有把真 process 假裝成成功。

## 流程怎麼走

`planned` 可直覺看成「已登記、可從 registry 找到」；`linked` 是「已接到 parent、才可往下做」。Receipt 是已完成結果的不可變收據。

```text
root Call → root_planned → task.json → accepted → root_linked
                                              ↓
first Call → child_planned → task.json → accepted → child_linked
          → Receipt payload → receipt_committed → child_observed
                                              ↓（符合 first 成功條件才繼續）
second Call → child_planned → task.json → accepted → child_linked
           → Receipt payload → receipt_committed → child_observed
                                              ↓
root Receipt payload → root receipt_committed
```

兩個 fake child 各自只有 `accepted → receipt_committed`，沒有 `dispatch_intent`、`attempts/`、P0 UUID 或 subprocess。若 first 的結果已知但不符合條件，parent 先記下 first，然後穩定停在 `waiting_for_parent_policy`；不會建立 second 或 root Receipt。

## 中斷與恢復的感覺

測試會在 10 個 root 切點及 23 個 Phase 2A 切點把 writer 以 exit 97 停掉。新 process 的 `recover` 先完整檢查已提交資料，只修可辨識的 JSONL 最後殘尾，再一次補一個唯一缺口；每寫一步都重驗。恢復後的 authority bytes 要與乾淨跑完一致，terminal 後再 recover 五次也不變。

它不會掃 `tasks/` 把孤兒目錄猜成 child，不會從原 input、resolver 或環境重新猜 recipe，也不會因為沒有已提交的「完成」紀錄就猜成功或自動重跑。若 root 尚未 `root_planned`，它仍未被接受；recover 會保持空白，須由呼叫端明確再 accept。遇到順序、ref、payload、目錄形狀等矛盾時，會 fail closed（停止並報錯），不拿後續資料硬補成合法。

## 檔案用途

- [aos_p1a2.py](aos_p1a2.py)：Phase 1 的 `accept` / `recover` 小型 CLI。
- [p1a2_store.py](p1a2_store.py)：嚴格 JSON、ref、鎖、原子寫入與 fsync 的檔案系統小工具。
- [p1a2_model.py](p1a2_model.py)：Phase 1 root 接受與恢復模型。
- [p1a2_composite_fake.py](p1a2_composite_fake.py)：Phase 2A 的雙 fake child 串接與恢復。
- [test_vectors.py](test_vectors.py)、[test_composite_fake_vectors.py](test_composite_fake_vectors.py)：固定 golden bytes／hash 與格式反例。
- [test_accept_recover.py](test_accept_recover.py)、[test_composite_fake.py](test_composite_fake.py)、[test_support.py](test_support.py)：斷點、恢復、容量、孤兒與竄改測試及共用 helper。

## 已驗結果與重跑方式

已在 **WSL Ubuntu** 執行；測試 helper 會在 Linux 的 `/tmp` 建立暫存工作目錄。最終結果是 **58 tests，44.001s，OK**。

```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /mnt/c/code/mine/simple_tools/agent-machine/workbench/2026-08-14/p1a2-process-python && PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v'
```

已驗範圍包括：canonical UTF-8 JSON／golden bytes、root 接受與同 ID 的安全拒絕、單 writer lock、合法 torn tail 修復、10+23 crash cuts、root Call 封閉性（刪除 input、改 FIFO、poison resolver）、兩個 fake child 的順序、oracle mismatch、孤兒不進 tree、容量邊界，以及已提交資料遭竄改時停止。

## 還沒驗，不可外推

沒有驗**真 process** 執行，也沒有驗 Agent、正式 scheduler、斷電／掉電語意、多 writer 正確性、跨機或不同檔案系統。雖有單鎖的競爭測試，仍不表示可安全支援多 writer；也不表示 NFS、TOCTOU、GC、portable checkpoint 或正式 `.aos` 格式已成立。

使用上它像一個很小、很嚴格的恢復沙盒：狀態少、錯誤早報，拿來看「停在這一步後還能不能接回」很直接；但 Phase 2A 沒有公開 CLI，需由 Python 的 `CompositeFakeRuntime` 驅動，不能當成可直接部署的排程器。
