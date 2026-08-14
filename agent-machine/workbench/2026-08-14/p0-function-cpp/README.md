# AOS Function：C++23 工作台原型，不是規格

這個小專案只把一次 Linux process call 固化成可測的 adapter。它不包含 Task、queue、agent、daemon、SQLite、crash recovery protocol 或正式 schema；runtime 與 build 請放 WSL 的 Linux filesystem（如 `/tmp`），不是 `/mnt/c`。

CLI 以 `--` 後第一個值同時作 `executable` 與暫定的 `argv[0]`：

```bash
cd /mnt/c/code/mine/simple_tools/agent-machine/workbench/2026-08-14/p0-function-cpp
cmake -S . -B /tmp/aos-p0-cpp-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/aos-p0-cpp-build -j
ctest --test-dir /tmp/aos-p0-cpp-build --output-on-failure
printf 'abc\0x' >/tmp/aos-input
/tmp/aos-p0-cpp-build/aos_p0_runner --out-dir /tmp/aos-one --stdin /tmp/aos-input -- /bin/cat
```

結果目錄有 raw `stdout.bin`、`stderr.bin` 及簡單 `receipt.json`。`stdout` 與 `stderr` 是分流的 bytes；runner 自己的 stdout 是同一份 receipt JSON。沒有 shell、字串命令列拼接或 PATH lookup（使用 `execve`）。明確 `--cwd` 可測試 chdir。

核心實作是單執行緒的 `fork`：CLOEXEC launch-error pipe 讓 child 在 `chdir` 或 `execve` 失敗時回報 errno，而正常的 child `exit(127)` 仍是 `exited(127)`。child fork 後只作 fd 操作、chdir、execve、write、_exit；parent 用 nonblocking pipe 與 `poll` 同時餵 stdin、讀 stdout/stderr，再以 `waitpid` 正規化 exited/signaled。它處理 EINTR、關閉未用 fd；parent 暫時忽略 SIGPIPE，將 EPIPE 當 stdin 已關閉，但 child 在 exec 前以 async-signal-safe `sigaction` 恢復 `SIGPIPE=SIG_DFL`。因此 adapter 自己的 signal handling 不得洩漏到 Function。本次只固定 SIGPIPE disposition；其他 signal mask/default policy 尚未宣告。

`differential.py` 會用同一個 C++ fixture 同時經 Python P0 oracle 與 C++ runner，比較 termination、stdout、stderr、cwd 和 argv，並含 16 MiB delayed-read、雙大量 output、SIGPIPE default、parent EPIPE、ENOENT、EACCES、signal 及 12 次重跑。唯一刻意的格式差異是 Python 把 launch 失敗標為 `spawn_error/stage=spawn`，C++ receipt 標為 `launch_error/stage=exec|chdir`；errno 與其他核心結果相同。

使用感受是低階 process 邊界比 schema 更早暴露了關鍵事實：stdin、兩個輸出管與 wait 不能順序處理。限制是輸出仍累積在記憶體、沒有 timeout/resource limits、沒有 env policy、沒有 hash/fsync/atomic persistence，也沒有保證跨 WSL、container 或檔案系統的 durability 語義。
