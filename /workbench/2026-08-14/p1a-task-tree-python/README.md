# P1a-1：fake composite Task tree

> 工作稿，不是正式 AOS 規格或 durability 承諾。

這個 Python/std-lib 小實驗只驗一件事：一個固定兩格的 composite Function，能否把 child Call、child Task、語意 Receipt 的關係安全地寫進單機檔案 store，並在 writer process 被強制結束後反覆恢復。

它不會啟動 Linux subprocess；fake leaf 的結果是固定成功或固定不明。P0 的 process staging、不明結果的外部副作用、Agent/Step/Round、Git、`.aos` 與中央 Runtime 都不在此處。

## 兩分鐘重跑

請在 WSL Ubuntu 的 Linux filesystem 跑，不要把 `/mnt/c` 當 durability 證據：

```bash
cd /mnt/c/code/mine/simple_tools/agent-machine/workbench/2026-08-14/p1a-task-tree-python
python3 -m unittest -v
python3 aos_p1a.py --root /tmp/aos-p1a-demo --case success
cat /tmp/aos-p1a-demo/report.json
```

預期 unittest 對每個 fault point 開一個真正獨立的 Python writer process：第一次在命名 failpoint 以 `os._exit(97)` 結束，之後以十個新的 process 恢復。runtime 落在 `/tmp`。

## 這個小模型在做什麼

`parent` 的 fixed plan 是 `first -> second`。child ID 固定由 `(parent_id, slot)` 的 versioned SHA-256 公式得出。每格依序：

1. 在 **parent** 的 `payload/` 原子發布完整 immutable child Call bytes。
2. parent 寫 `child_planned`，保存相對 call path 與 hash。
3. child `task.json`／genesis 只引用同一份 path+hash，絕不複製 Call bytes。
4. parent 寫 `child_linked`；此後 child 才 eligible。

parent 的 planned/linked events 是 child 關係的唯一權威；程式不掃目錄猜關係。成功時兩個 child 都先 commit semantic Receipt，parent 再只引用有序的 `(child id, receipt hash)`，產生 `sequence complete\n`、空 stderr、logical status 0 的 Receipt。

unknown fixture 讓 `first` 在 durable `dispatch_intent` 後沒有 Receipt。恢復時 first 與 parent 都是 `repair_required`；不會重送 first，也不建立/派送 second。

同樣地，即使選的是 success fixture，若強制結束剛好落在 intent 之後、完整 fake Receipt bytes 發布之前，恢復也必須選擇 `repair_required`；它不能由「原本預期成功」反推已成功。

## 檔案與限制

`tasks/<id>/events.jsonl` 是每 Task 的小事件串；Call／Receipt 是 immutable JSON bytes。`report.json` 會列出 case、fault point、Task tree、state trace、dispatch count、receipt hash 與驗證錯誤。

這只是 failpoint + process-kill 實驗：驗到的是單一 writer 在同一 Linux filesystem 上的恢復邏輯。**沒有**驗拔電、裝置快取、NFS、multi-writer、exactly-once、外部副作用回滾或正式 filesystem contract。ID/path 拒絕非 64 位小寫 hex、symlink 和已知的 reference 衝突；這不是完整的安全模型。
