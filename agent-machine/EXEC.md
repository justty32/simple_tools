# AgentOS Linux exec tool 契約

本頁固定 `_type: "exec"` 如何把一包 JSON arguments 變成一個 Linux process。Python 與未來 C++ 必須讀
同一份 recipe、組出相同 argv。它不經 shell，也不是 sandbox。

## 完整外形

```json
"_extra": {
  "_version":"0.1.0",
  "_type":"exec",
  "exec":["../read-file"],
  "argv":{"path":{"position":1}},
  "stdin":null,
  "cwd":null,
  "timeout":60,
  "stdout":{"clip":"head", "max_bytes":65536},
  "stderr":{"mode":"merge"},
  "ok_exit":[0],
  "limits":{"path":{"max_bytes":4096}}
}
```

required/default/type/range 見 [`TOOL_SPEC.md`](TOOL_SPEC.md)。所有關係在 `tools check` 時驗證。

## executable 與 cwd

`exec` 是非空 string array。第一項是 executable，其餘是永遠放在前面的固定 argv：

```json
"exec":["git", "status", "--short"]
```

- `exec[0]` 是絕對 path：直接使用。
- 含 `/` 的相對 path：相對 tool spec 的 logical origin。由 `$ref` 載入時是來源 JSON 所在目錄；inline
  spec 則以 Bot root 為 origin。private `.agentos/` 位置不影響它。
- 不含 `/`：準備 instruction 時依 worker 的 `PATH` 查找第一個 executable regular file，並把 lexical absolute
  path 存進 instruction。PATH 缺少時使用 `/usr/local/bin:/usr/bin:/bin`；空或相對 segment 視同無法安全
  查找，固定回 `Error [executable_not_found]`。
- `exec[1:]` 只是固定參數，不當 path 重寫。
- `cwd: null` 表示 Bot root；相對 cwd 也以 Bot root 為基準。cwd 必須是現存 directory。

path 只做 [`CONFIG.md`](CONFIG.md) 的 lexical normalize，不做 `realpath`。executable、cwd 與所有 argv item
若含 U+0000 都在 dispatch 前拒絕；stdin 可以含 NUL。執行前仍重查 target 可執行，但 v1 不宣稱 path 指向
的 inode／bytes 從準備到執行都沒變。

AgentOS 直接呼叫 argv，永遠是 `shell=false`。arguments 裡的空白、`;`、`$()` 都只是普通字元。

v1 不接受 `_extra.env`，child 原樣繼承 dispatch 時 worker 的 environment。環境值不寫進 journal，也不宣稱
不同 shell session 一定可重播出相同外部結果。日後需要受控 secret/environment 時另加明確版本，不把它
偷偷塞進 arguments。

## JSON arguments 變成 argv

先用 `function.parameters` 驗證 arguments。未知參數、缺 required、型別不合都不執行 process，而是形成
有界的 `failed` tool result。每個 properties key 必須恰好被 `argv` 或 `stdin` 使用，不能宣告後默默忽略。

```json
"argv": {
  "path":{"position":1},
  "width":{"position":2, "flag":"--width"},
  "force":{"position":3, "flag":"--force"},
  "tag":{"position":4, "flag":"--tag", "repeat":true}
}
```

排序先看 `position` 小到大；同 position 依參數名的 Unicode code point 排。每個 mapping 只可有：

| key | default | 意思 |
|---|---:|---|
| `position` | 0 | argv 排序 |
| `flag` | 無 | 有值時放在值前面 |
| `separate` | true | false 時形成 `--width=800` 一項 |
| `repeat` | false | true 才接受 array，逐項展開 |

- optional argument 缺席就略過；null 不在 v1 tool schema 型別內，因此拒絕。
- 沒 flag：只放值。有 flag 的一般值：放 flag、再放值。
- boolean 有 flag：true 只放 flag，false 什麼都不放。
- `repeat:true` 的 array 逐項套同一規則；否則 array／object 拒絕。
- `separate:false` 必須同時有 flag；沒有 flag 是壞 spec。stdin param 不得再出現在 argv mapping。
- string 原樣成為一個 argv item；integer 使用普通十進位；boolean 使用小寫 `true/false`。
- integer/number 範圍與精確文字直接使用 [`JSON.md`](JSON.md) 的 JCS number；因此 `1.0` 會形成 `1`，
  `-0` 形成 `0`，不能依語言的 default float formatter。

單一 item 與完整 argv（executable、fixed、dynamic）的 UTF-8 bytes 各不得超過 131072；超過不啟動 process。

## stdin

```json
"stdin":{"param":"text"}
```

這表示 string 參數 `text` 不進 argv，改以 UTF-8 原樣寫入 stdin。optional 參數缺席就寫零 bytes。只能指定
一個 properties key；大小由 `limits.text.max_bytes` 控制。`stdin:null` 時 child 仍收到立即 EOF，不可繼承
client stdin，否則背景 tool 可能永遠等輸入。

## timeout、output 與 exit code

- `timeout` 是正數秒，default 60。tool 在新 process group 執行；超時先送 SIGTERM，兩秒後仍未結束就對
  整組送 SIGKILL，形成 `failed` result。已發生的檔案或網路效果不假裝 rollback。
- `stderr.mode` 是 `merge`（default）、`ignore` 或 `only`。merge 在 OS pipe 層把 stderr 接到 stdout，保留
  真實交錯順序；only 丟掉 stdout；ignore 丟掉 stderr。
- `stdout.clip` 是 `head`（default）或 `tail`。`stdout.max_bytes` default 65536，範圍 64..1048576；runtime
  必須一邊 drain pipe、一邊只保留這個上限，不能先無限吃進 memory 再裁。
- `ok_exit` 是不重複的 integer array，default `[0]`。exit code 在其中是 `done`；不在其中是 `failed`，
  content 使用下方固定模板。

`limits` key 必須是 arguments property；每項只接受 `max_bytes/min/max`。max_bytes 只用於 string，計 UTF-8；
min/max 只用於 number/integer。超標在 process 前形成 `failed` result。

## result 文字固定格式

tool content 會影響下一次 LLM，所以不能直接塞 Python exception 或 locale-specific `strerror`。第一行只用：

| case | 第一行 |
|---|---|
| arguments 不合法 | `Error [invalid_arguments]: JCS_DETAIL` |
| PATH 找不到 target | `Error [executable_not_found]` |
| process 無法啟動 | `Error [launch_failed]: errno=N` |
| stdin 未完整送達 | `Error [stdin_write_failed]` |
| timeout | `Error [timeout]: N seconds` |
| signal 結束 | `Error [signal]: N` |
| 不在 ok_exit | `Error [exit]: N` |

invalid argument 的 detail、reason 與排序完全依 [`TOOL_SPEC.md`](TOOL_SPEC.md)。有 selected output 時接一個
newline 再接 output；沒有就只有第一行。stdin EPIPE 後仍 wait child：若 child 已有 non-ok exit/signal，
以 exit/signal 為主；否則用 stdin_write_failed。timeout 的 N 使用 JCS number。

runtime drain 完整 selected stream，追蹤 total bytes 與全流 NUL：

- 任一 NUL：output 固定為 `(binary output, N bytes, not shown)`。
- 未超限：依 UTF-8 解碼。invalid sequence 與切到一半的 sequence 依 Unicode maximal-subpart 規則各換成 U+FFFD。
- head 超限：`DECODED_HEAD\n...[N bytes clipped]`。
- tail 超限：`[N bytes clipped]...\nDECODED_TAIL`。

`max_bytes` 只限制保留的 process bytes；ASCII clip marker 不算在內。N 是完整 stream 減 retained bytes。
成功且沒有 selected output 固定回 `(no output, exit N)`。這些完整字串與 UTF-8 edge cases 都進跨語言 fixtures。

## 一條 durable instruction

準備 tool instruction 時，runtime 保存 tool spec hash、已解析 executable、完整 argv、stdin hash/size、cwd、
timeout 與 output policy。不要只保存來源 `$ref`，否則共用檔改動會讓已準備的工作變樣。敏感 stdin 預設放
immutable result/input file，不直接塞進人類 log。

process 正常回來、timeout 或確定無法啟動，都能形成合法的 `done/failed` result。只有 durable
`dispatching` 已存在、worker 卻消失且沒有可靠 result 時才是 `unknown`，不得自動重送。exec 擁有目前 Linux
user 的完整權限；step mode 是檢查介面，不是隔離。真的要限制任意程式，必須另用 container／VM／OS sandbox。
