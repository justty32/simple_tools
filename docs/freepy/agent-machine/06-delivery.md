# 交付順序與驗收

每個階段都要留下可執行程式、離線測試與一份 machine-readable trace；不能只完成 class skeleton。
順序依賴上一階段的資料契約，後面的功能不得反過來污染現有 `agentloop` API。

## PR 切片

| 次序 | 交付內容 | 必過 gate |
|---|---|---|
| 1 | Node/DirectoryEntry/Operation/Commit、path、immutable snapshot | strict parse、pure replay、root CAS、idempotency、pairing property |
| 2 | directory backend、journal/HEAD recovery、CLI 範例 | 每個寫點 crash injection；結果只有 G 或 G+1 |
| 3 | versioned mount 與 derived Git checkpoint | pending repair、expected-oid conflict、diff；不碰外部 effect |
| 4 | generic executor + deterministic VFS executor | worker 無 store pointer；result 經 validator 才 commit |
| 5 | llmkit + frozen exec adapter；agentloop compatibility bridge | 短命 Bot、manifest/usage 完整；call/result pairing 不重做 |
| 6 | provider model lifecycle + exclusive residency transition | draining 擋新 admission；refcount=0 才 unload；race/final cleanup |
| 7 | durable scheduler、lease、ledger、progress | fairness、late result、429 backoff、exactly-once settlement |
| 8 | refs、archive-first GC、自我改進 branch | active lease/pin 安全；promotion/rollback 有 evidence |
| 9 | freeze v1 corpus，建立 C++ differential CLI | Python/C++ 逐欄一致，sanitizer/fuzz 綠 |
| 10 | C++ transaction/scheduler + embedded Janet policy | Janet 無直接 effect；policy env 只進 owner thread；shadow policy 一致 |
| 11 | Linux worker enforcement、可選 9P/FUSE | 真實 isolation evidence；無支援平台 fail closed |

PR 1 的檔案範圍應刻意窄：`ids.py`、`records.py`、`protocol.py`、`paths.py`、pure `store.py` 與 checks。
先不定 Function/Round/Instruction，也不加入 durable journal、Git、HTTP、真模型、scheduler 或 Janet。

## 測試層次

### 1. Pure contract

- JSON/JDN 邊界：Unicode、unknown/duplicate field、版本、整數溢位、canonical order。
- Path：`.`/`..`、separator、case policy、reserved `.name`、rename/move pairing、alias cycle。
- Reducer：同 events 得同 state；未知 event fail closed；operation idempotency。
- Ledger：reserve/charge/refund 守恆；parent/child 不超賣；settlement 一次。
- Refs：shared/cycle/pin/lease/Git retention 的 reachability property tests。

### 2. Fault injection

在 stage write、manifest、fsync、journal prepared、HEAD publish、Git ref update、result settlement 每一點
終止 process，再啟動 recover。外部 tool 另測 before-send、after-send/no-receipt、receipt-before-commit。

預期永遠是：可證明未執行、已提交，或 `outcome_unknown`；不能靠猜測自動重送。

### 3. Concurrency

- 兩個 writer 對同 root generation，恰好一個成功。
- pause/cancel 與 result 同時到達，在單一序列化點決定。
- lease expiry/reassign 後舊 worker 回傳，不能污染新 generation。
- GC snapshot 與新 ref/active read 競爭，不刪 live object。
- Git active-ref promotion 與使用者更新競爭，CAS conflict 可見。

### 4. Scheduler simulation

使用 fake clock、fake stochastic LLM 和 scripted failures，不用真網路：

- 10,000 dormant Functions、數百 queued Rounds、固定 K slots，active memory 與 thread 數有界。
- 兩個 owner 長短工作混合都不飢餓；foreground 能快速看到 progress。
- token estimate 偏差依 actual charge 修正；長 prompt 不永久餓死。
- endpoint 429/5xx 共享 backoff，沒有 retry herd；deadline miss 有正式結果。

### 5. Adapter/live smoke

現有離線基線繼續跑：`base_tools`、`exec_tools`、`http_tools`、`agentloop`、`tooljson`、Pi bridge。
新 adapter 使用 fake OpenAI server 覆蓋 malformed tool args、truncation、disconnect、usage-only、timeout。

Ollama live smoke 是選配但有固定清理規則：預設每 Step 給 4096 output tokens、12 Steps、8 calls、
900 秒；切模前確認舊模型無其他 lease，unload 並查 residency，`finally` unload 最後模型。Live failure
不阻塞純核心 CI；預設只保存 redacted manifest hash、model revision、usage 與清理結果，raw prompt/
tool output 必須明確 opt in，避免測試 trace 洩漏資料。

### 6. Native

- Python/C++/Janet 共享 golden vectors 與 traces，diff 指出 field/transition，不只比較整段文字。
- C++ parser/path/reducer fuzz；ASan/UBSan/TSan；process tree cleanup 與 fd/handle leak。
- Janet runtime/env init/reload/shutdown、non-local error/RAII 邊界、handle registry、buffer ownership、
  owner-thread enforcement；不受信 policy process timeout。
- 空目錄 CMake build、install tree smoke、無 source tree 啟動；Windows DLL-lock 情境另測。

## 階段完成定義

Python reference 可稱為完成，必須同時達成：

- 單機重啟後，queue、Round、generation、usage 與已提交檔案能恢復。
- 一個含 LLM + tools 的 Round 能在 safe boundary pause/resume，且 replay 不重呼模型或工具。
- 多 Round 共享 endpoint 不超賣、不餓死；stale/duplicate result 不提交。
- Git 能顯示、branch、promote；active ref 可 CAS 回舊 revision，外部效果不被誤稱可 rollback。
- GC 預設 archive-first，所有刪除都有 generation/pin/lease 檢查。

Native 版本可成為 default，還需：

- v1 corpus 與 Python oracle 在約定欄位 100% 一致，允許差異有版本化說明。
- C++ core 是唯一 commit authority；Janet policy 拔掉或 crash 時仍保持資料與權限安全。
- Linux clean build、sanitizer、fuzz、crash recovery 和至少一個真 executor smoke 維持綠。
- 操作文件能從空 workspace 建 Function、執行、檢查、恢復、切模、GC 和 rollback。

## 需要量測，不先猜答案

- directory/content-addressed backend 是否足夠，何時需要 SQLite index。
- 一條 commit loop 在多少 runnable Rounds 後成為瓶頸。
- Git checkpoint 粒度對 repo size、latency 與人類 diff 品質的影響。
- embedded Janet 與 stdio Janet service 的故障隔離／延遲差。
- C++ native transport 是否比 Python/LiteLLM adapter 有實際部署收益。

量測結果以 trace/benchmark 留檔，再做 ADR。沒有數據前不提前增加 shard、binary protocol、native TLS、
CPython embedding 或 kernel module。
