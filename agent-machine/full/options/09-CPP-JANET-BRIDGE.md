# C++／Janet bridge 選項

狀態：**尚未決定**。目前可主張的工具鏈證據很窄：WSL 的 C++23 P0 已對 leaf process 比較 argv、raw streams、cwd、exit／signal／launch error，含雙大量輸出與 16 MiB delayed-read；它沒有 durable writer、scheduler、hash／fsync、timeout 或正式 schema。Windows Janet `1.41.2-local` 的純資料 policy 有 25 tests passed；WSL 目前沒有 Linux Janet executable、header 或 library，所以 Linux embed／FFI 尚未驗證。

責任方向見[實作與證據](../12-IMPLEMENTATION-AND-EVIDENCE.md)：C++ 守住 process、fd、lock、fsync、證據重驗與正式 commit；Janet 只讀已驗證 snapshot，回傳 policy proposal。Janet 不取得 store path、fd 或寫入 handle，也不能捏造 Return／Receipt。

## 方案一：兩個 process 傳有界資料

C++ 將版本化、已驗證的 snapshot 送給 Janet process；Janet stdout 只回建議，stderr 作診斷。

- **優點**：故障隔離最好；可獨立殺掉與替換；輸入輸出容易保存成 golden corpus。
- **缺點**：啟動與編解碼有成本；要固定 timeout、大小、非零 exit 與壞輸出行為；不適合極高頻 callback。

這是 Linux Janet gate 通過後的第一個整合原型推薦。

## 方案二：C++ 內嵌 Janet VM

C++ Runtime 建立 VM，直接呼叫 policy function。

- **優點**：部署集中；呼叫成本低；資料轉換可縮小。
- **缺點**：VM crash 與 Runtime 同一故障範圍；thread、fork 前後初始化與升級更難；目前 Linux toolchain 尚未證明。

只有 process 方案量出明確效能瓶頸後再比較。

## 方案三：Janet 主程式載入 C++ native module

Janet 負責外層流程，C++ module 提供 process／store primitives。

- **優點**：REPL 與 policy 開發最舒服；規則替換快。
- **缺點**：正式寫入權容易滑到 Janet；module ABI、錯誤隔離與 Windows DLL 重建較麻煩。

適合開發工具，不優先作唯一 Runtime。

## 方案四：小型 C ABI／FFI

Janet 透過很窄的 C ABI 呼叫 pure validator 或 decision seam。

- **優點**：介面面積可控；適合快速試驗資料 ownership。
- **缺點**：pointer 壽命、allocator、callback 與 ABI 版本一錯就可能 crash；跑通不等於 durable 邊界安全。

只作小原型，正式 store commit 不經 FFI handle 暴露。

## 共同 bridge 契約候選

輸入應是有版本、大小上限且已脫離 live store 的 snapshot；輸出只含 observed-state reference、decision、ordered Call proposals 與理由。C++ 收到後重新驗證版本、root 資格、path、argv 與容量，再決定是否持久提交。

未知欄位、重複 key、過大輸出、過期 reference、timeout、crash、stderr 或非法 Call 都只能讓工作停在可診斷狀態，且正式 store 零寫入。精確 JSON、hash 與 C ABI 都尚未固定，不可把 P0 sample 當公開 ABI。Janet marshal 也只適合本機除錯，不作 portable Task state。

## 目前推薦

先完成 Python 語意與 C++ durable seam，再通過 Linux Janet toolchain gate；第一個 bridge 用方案一證明「proposal 不具 authority」。保留相同 corpus 比較 embed／native module／FFI，只有實測需求才縮短故障隔離邊界。

## 驗證與推翻條件

1. WSL 能列出 Janet 版本、header、library，完成最小 compile／run；缺件時明確 blocked，不用 Python 冒充。
2. Python 與 C++ 對相同 leaf fixtures 的 termination 與 raw bytes 一致；現有刻意格式差異先被解釋或正規化。
3. Janet 對同一 immutable snapshot 給穩定 proposal；壞 JSON、超時、crash、非法 child Call 與偽造結果都使 store bytes 不變。
4. C++ 在 Janet 回覆後故意換 state version，必須拒絕過期 proposal；Janet 不得繞過重驗。
5. process 與 embed 對同一 corpus 的 decision／error 等價；embed 另通過 thread、fork、VM 重建與故障測試。
6. 量測 proposal 頻率、資料大小與延遲；只有 process overhead 造成實際瓶頸，且 embed 仍通過隔離測試，才推翻第一接法。

現有證據可追到[C++ P0](../../workbench/2026-08-14/p0-function-cpp/README.md)、[Janet P0](../../workbench/2026-08-14/p0-function-janet/README.md)與[bridge 候選](../../workbench/2026-08-14/idea-ledger/CPP-JANET-PHASE-CANDIDATES.md)。
