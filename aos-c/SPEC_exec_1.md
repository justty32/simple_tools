# 規格 2:執行模型改採「shell 等級忠實度」,拿掉回報管道

狀態:**已實作**(commit 70bf3a3)。與 [SPEC_env_1](SPEC_env_1.md) 一起構成
inst_t → 執行的重整。126/127 照建議分開。

> 後續修訂:這份提到保留 `PlatformUnsupported`,那已經不成立 —— 專案改成只支援
> POSIX,非 POSIX 分支連同這個狀態一起拿掉了(C ABI 的 5 號留空)。其餘結論不受
> 影響。

## 定案的核心決定

採「模型 A」:aos-c 啟動子行程、等它結束、把一個 exit code 寫進 exit_path,
**不多嘴**。跟 shell 一樣,不去分辨「指令根本沒起來」和「指令跑了、自己回傳
127」。

- **exit_path 是「這筆做完了」的信號,不是「做對了」的信號。** 關注結果的人
  等 exit_path 出現/有內容,就知道這筆結束;指令做得對不對,另外看
  stdout/stderr。
- 因此 **exit_path 有設就無論成敗一律寫一個碼**(這正是舊設計故意不做、卻
  與「消費端讀檔」模型衝突的地方——現在改過來)。exit_path 空著才丟棄。

## exit code 慣例(沿用 shell)

| 情況 | 寫進 exit_path 的碼 |
|---|---|
| 子程式正常結束 | 它自己的 exit code(0–255) |
| 被訊號終止 | 128 + 訊號編號 |
| execvp 失敗(找不到指令等) | 127 |
| exec 前的佈置失敗(重導向 open / chdir) | 126 |

子行程端邏輯:重導向/chdir 失敗 → `_exit(126)`;execvp 返回(即失敗)→
`_exit(127)`。**不再有管道寫入。**

## exec 流程(拿掉管道後)

1. 父行程:驗 argv(空 → `InvalidArgument`)。
2. `fork()`。fork 本身失敗 → `SpawnFailed`(父行程端,沒有子行程可以變成
   exit code,只能這樣回報)。
3. 子行程:重導向(open+dup2)→ chdir → 對 `inst.env` 的每一筆呼叫 `setenv`，在
   繼承的環境上覆寫同名、新增其餘 → execvp。任一步失敗就 `_exit(126/127)`,不返回。
4. 父行程:`waitpid` 回收(仍需要:循序執行、收殭屍、取 status)。解讀:
   訊號 → 128+sig、正常 → exit code。
5. exit_path 有設 → 寫「碼 + 換行」;寫不進去 → `ExitWriteFailed`。
6. 回傳。**注意:子程式回傳 126/127 對 execute() 而言是 `Ok`**(這筆有跑完、
   有產出一個碼),不是 aos-c 的失敗。

## 影響面

### `src/exec.cpp`——大幅變短
- 刪掉:CLOEXEC 回報管道(`pipe`/`fcntl`)、`ChildError`、`child_fail`、
  `Stage`、`stage_to_state`、父行程 `read` 管道那段。
- 子行程失敗改成 `_exit(126/127)`。

### `include/aos/exec.hpp`——`ExecState` 縮小
- 保留:`Ok`、`InvalidArgument`、`SpawnFailed`(僅剩 fork 失敗)、`WaitFailed`、
  `ExitWriteFailed`、`PlatformUnsupported`。
- 移除:`OpenStdinFailed`/`OpenStdoutFailed`/`OpenStderrFailed`/`ChdirFailed`
  (→ 變成 exit code 126)、`EnvFileFailed`(env 已 inline,見 SPEC_env_1)。
- `ExecResult`:`status` 照舊;`signalled` 保留(給行程內呼叫端);`error`
  now 只在 fork/wait 失敗時有值,保留但很少設。
- 更新 exit_path 欄位語意的註解:改成「一律寫一個碼」。

### C ABI(`src/capi.cpp` + C 標頭)
- 移除被刪掉的 ExecState 對應的 C 列舉值與 `AOS_CHECK_STATE`;更新
  `aos_exec_state_string` 的範圍判斷。ExecState 重新編號 = ABI 破壞,原型階段
  可接受,`static_assert` 會抓對不齊。

### `src/run.cpp`
- exec 失敗大幅變少:找不到指令不再是 aos-c 的失敗,而是一筆正常產出 127 的
  記錄。`failed` 計數只剩 fork/wait/寫檔 這類真正的執行期錯誤。

### 測試 + docs
- 原本斷言 `OpenStdinFailed`/`ChdirFailed` 的測試,改成斷言 exit_path 內容為
  126/127。新增:找不到指令 → exit_path=127、重導向失敗 → 126、一律寫碼、
  訊號 → 128+sig。docs 同步。

## 待確認(小)
- **126 / 127 要不要分?** 分是 shell 慣例、免費又眼熟;若要更極簡可全用 127
  (「aos-c 沒能照指示跑起來」一個碼)。*建議:照 shell 分 126/127。*

## 驗證
- WSL Ubuntu:`make clean && make test`,全綠、零警告。

## 未受影響(釐清)
- 這份只動 exec 的「回報方式」,不動「怎麼生行程」——fork/execvp 仍是最小正確
  解(見 DISC_CONC_1)。
- DISC_CONC_1 提的 **insts 讀取 fd 的 CLOEXEC 洩漏**是另一回事(insts fd 漏進
  子程式),與這裡拿掉的回報管道無關,走串流前仍要處理。
