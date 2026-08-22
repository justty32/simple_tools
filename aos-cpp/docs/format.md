# 記錄格式

`aos-cpp` 讀取一份完整的 JSON 文件。頂層可以是：

- 一個指令物件，代表單筆執行。
- 一個由指令物件組成的陣列，代表依序執行的批次；空陣列是合法的空批次。

文件可使用一般 JSON 空白與縮排。JSON Lines（連續放置多個頂層物件）不是合法
輸入；多筆指令必須放進同一個陣列。runner 會先讀到 EOF，再解析並驗證整份文件，
任何一筆失敗都不會執行其中任何指令。

## 綱要(schema)

| 鍵 | JSON 型別 | 必填 | 預設 | 意義 |
| --- | --- | --- | --- | --- |
| `argv` | array of strings | yes | none | 指令及其引數。此陣列與 `argv[0]` 都不得為空。 |
| `stdin` | string | no | `""` | 以唯讀方式開啟、作為標準輸入的檔案；為空時繼承呼叫端的 stdin。 |
| `stdout` | string | no | `""` | 作為標準輸出的檔案，必要時建立並截斷(清空)；為空時繼承 stdout。 |
| `stderr` | string | no | `""` | 作為標準錯誤的檔案，必要時建立並截斷(清空)；為空時繼承 stderr。 |
| `exit` | string | no | `""` | 子行程結束後建立/截斷(清空)的檔案，寫入十進位狀態值加一個 LF；為空時捨棄。 |
| `cwd` | string | no | `""` | 子行程的工作目錄；為空時繼承呼叫端的目錄。相對路徑值從呼叫端的目錄起算。 |
| `env` | object, string values | no | `{}` | 在繼承的環境之上覆寫或新增變數；未提及的變數維持不變。 |
| `timeout_ms` | unsigned integer | no | `0` | 執行時間上限（毫秒）；為零時無期限等待。 |

環境（變數）的 key 必須非空，且不得含有 `=`。JSON 物件的 key 在記憶體中的
指令裡是唯一的；來源中重複的 key 由 JSON 解析器處理，而非提供一套有序的
覆寫機制。

以下這筆完整記錄會在 `/tmp` 底下執行 `sh`、提供一個環境變數、
重導向全部三個標準串流、記錄狀態，並施加
五秒的上限：

```json
{"argv":["sh","-c","read line; printf '%s: %s\\n' \"$LABEL\" \"$line\"; printf 'diagnostic\\n' >&2"],"stdin":"/tmp/aos-input.txt","stdout":"/tmp/aos-output.txt","stderr":"/tmp/aos-error.txt","exit":"/tmp/aos-status.txt","cwd":"/tmp","env":{"LABEL":"worker"},"timeout_ms":5000}
```

被引用的 `/tmp/aos-input.txt` 必須事先存在。成功時，本範例會
寫出記錄中指定的另外三個 `/tmp/aos-*` 檔案。

## 驗證狀態與上限

| 條件 | `InstState` / C 狀態 |
| --- | --- |
| 輸入指標為 null | `InvalidArgument` / `AOS_INST_INVALID_ARGUMENT` |
| JSON 無效，包含單筆記錄的空緩衝區 | `JsonSyntax` / `AOS_INST_JSON_SYNTAX` |
| 單筆值或陣列元素不是物件；批次頂層不是物件或陣列 | `NotAnObject` / `AOS_INST_NOT_AN_OBJECT` |
| key 不在綱要(schema)內 | `UnknownKey` / `AOS_INST_UNKNOWN_KEY` |
| 欄位型別錯誤、引數非字串，或環境（變數）值非字串 | `FieldTypeMismatch` / `AOS_INST_FIELD_TYPE_MISMATCH` |
| `argv` 缺少/為空，或 `argv[0]` 為空 | `EmptyArgv` / `AOS_INST_EMPTY_ARGV` |
| 引數超過 256 個 | `TooManyArgs` / `AOS_INST_TOO_MANY_ARGS` |
| 環境（變數）項目超過 256 個 | `TooManyEnv` / `AOS_INST_TOO_MANY_ENV` |
| 環境（變數）key 為空，或 key 含有 `=` | `EnvKeyInvalid` / `AOS_INST_ENV_KEY_INVALID` |
| 解析時物件/陣列巢狀超過 3 層 | `DepthExceeded` / `AOS_INST_DEPTH_EXCEEDED` |

**輸入大小沒有上限。** 單筆記錄與整份文件都不設位元組上界，讀取端一路讀到
EOF。上界由呼叫端的環境負責（記憶體、`ulimit`、cgroup），不由這個格式負責。
唯一仍然硬性存在的結構性上限是巢狀深度 3、`argv` 256 個、`env` 256 條。

深度檢查發生在解析過程中，在深度巢狀的文件被完整建構起來之前就會攔下。
陣列元素驗證失敗時，CLI 會印出以 1 為起始的
record 序號並回傳 1；整份文件的 JSON 語法錯誤則只報告來源。該批次中不會有
任何記錄被執行。

未知的 key 是刻意拒絕的，而不是忽略。否則較舊的執行檔
可能會默默執行一筆含有較新安全欄位（例如
`timeout_ms`）的記錄，卻完全沒有 timeout。拒絕也能把像
`"stdou"` 這樣的拼寫錯誤變成明確的失敗，而不是默默失去重導向。

`write_one` 會先驗證整個指令，才會附加任何內容。它只輸出
非預設的選用欄位、緊湊的單一 JSON 物件，以及最後一個 LF；它不負責組裝批次陣列。
