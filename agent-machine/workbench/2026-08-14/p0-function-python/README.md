# AOS Function：Python 工作台原型，不是規格

這是一個只驗證一件事的 walking prototype：把「一次完整的 Linux process call」當作 AOS 的 Function／CPU 指令，把 Linux filesystem 當作記憶體。它刻意小到不含 Task、queue、agent、daemon、SQLite 或 Git；不能由此推導正式 AOS schema。

## 由小到大：一次呼叫

`Request` 明確分開 `executable` 與 `argv`，stdin/stdout/stderr 都是 raw bytes。它永遠以 `shell=False` 呼叫 `Popen`，不拼接命令列；`communicate()` 同時餵 stdin、排空 stdout/stderr，這裡只是 Python 的行為 oracle。CLI 的 `run -- PROGRAM ARG...` 暫時物化為 `executable=PROGRAM`、`argv=[PROGRAM, *ARG]`；`argv[0]` 應否可獨立指定仍是待比較問題。

每次呼叫是一個 invocation 目錄：先持久化 `request.json`、`stdin.bin` 與 request-ready；再持久化 dispatch intent；才 spawn。完成後依序持久化 stdout、stderr、含各自 SHA-256/size 的 receipt，最後寫 terminal marker。非零 exit 仍是完整 receipt；結果會是 `exited(code)`、`signaled(signal)`、`spawn_error(stage, errno)` 或 `outcome_unknown`。

`recover` 不重跑任何有 dispatch intent 的工作。它會先嚴格驗證現有 receipt：request-ready/intent 關係、receipt 結構、已分離輸出的 size 與 SHA-256 都吻合時，即使 `receipt-ready` 尚未發布，也只補 ready/terminal，不覆蓋輸出。receipt 已完整但缺 terminal 時，同樣只補 terminal projection。

反之，只有 stdout、遺留的 `.` temporary file、無法解析的 receipt、或 hash/size 不符，都不是可信完整輸出。recovery 會把這些 result-side artifacts 移入 invocation 內的 `quarantine/`，寫出 `quarantine.json` 原因，再投影 `outcome_unknown`；`inspect` 的 `receipt_valid`、`receipt_validation_error` 與 `quarantine` 會明示這個判斷。這仍不會自動重跑。

Invocation ID 是原型自行生成的 32 位小寫 hexadecimal 值。create、inspect、recover 都只接受此格式，並只映射到 memory root 的直接子目錄；絕對路徑、`..`、slash、大小寫或長度不符都會拒絕。為免暗中擴張 symlink 語義，既有 invocation directory 若是 symlink 也會拒絕。

## 在 WSL Ubuntu 實跑

來源可放 Windows 掛載路徑，但 Linux durability 的 runtime 必須放 Linux filesystem，例如 `/tmp`：

```bash
cd /mnt/c/code/mine/simple_tools/agent-machine/workbench/2026-08-14/p0-function-python
python3 -m unittest -v
python3 aos_p0.py --root /tmp/aos-memory run -- /bin/echo 'a b' '*.txt'
```

沒有第三方依賴。測試各自在 `/tmp` 建 runtime；請勿把 Windows subprocess 或 `/mnt/c` 上的 `fsync` 當成 Linux durability 契約。

## 目前感受與限制

這個模型對「是否嘗試執行」與「可確定結果」的分界很直觀，尤其 crash 後不重跑能保住 side effect 的誠實性。反過來說，它還沒有宣告真正的 crash-consistency protocol：檔案系統、rename、directory fsync、容器/WSL 邊界和跨機器語義都需在正式設計中明定。`communicate()` 不是目標 runtime 的 scheduler，只是一個足以驗證雙 pipe 不死鎖的 Python 參考實作。
