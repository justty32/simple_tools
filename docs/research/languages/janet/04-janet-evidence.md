# langlab-janet 實證

實際 repo 位於 `C:\code\mine\langlab-janet`。它不只是教學：已有可對照 freepy 的 HTTP/tool-loop 與 subprocess 模組。

## 已證明

| 能力 | repo 證據 | 對 freepy 的意義 |
|---|---|---|
| JSON encode/decode | `spork/json` 文件與測試 | schema、card、manifest 可直接表示 |
| OpenAI-compatible HTTP | `modules/llm-http` | `llms` 可有 Janet transport frontend |
| 多步 tool loop | fake backend wire test | agentloop 基本 messages/call/result 閉環可行 |
| vision payload | media 模組與測試 | image content shape 可移植 |
| fiber/event/channel | `docs/09`、`docs/15` | 可實作非同步控制與 mailbox consumer |
| subprocess | `modules/pi-shell` | 有 `os/spawn`、pipe、wait、exit code API |
| FFI/native/embed | `docs/10*`、C examples | 能接 C，但要自負 ownership 風險 |
| executable build | `project.janet` | 固化核心可包成獨立 CLI |

`test/llm-http-server.janet` 在同一 process 啟假 OpenAI backend，實際驗證 request wire、tool_calls、tool result、第二步回答、舊測試欄位 `max-rounds`（語意是最大 Steps）、unknown handler、finish reason、usage 與自訂參數。這比僅看 API 文件更能支持移植。

## 本次實跑

`jpm test` 的結果：

- `basic.janet` 通過。
- 6 個 `llm-http` 測試通過：CLI、config、media、params、registry、server。
- 3 個 `pi-shell` 測試在 Windows 以 `-1073741819` 結束，即 access violation。
- `jpm` 最後回傳失敗。

因此「Janet 能跑 subprocess」只在 API 與部分 repo 實作層成立；此 Windows 環境沒有通過部署關卡。Linux 上仍應另跑完整 CI，不能從 Windows crash 反推 Linux 必敗，也不能忽略它。

## 關鍵限制

### HTTP

`spork/http` 沒有 TLS，也沒有 streaming/SSE。當前可走：

- Janet → 本機 LiteLLM proxy 的 plain HTTP。
- Janet → `curl` subprocess。
- 寫／採用具 TLS/SSE 的 native 或外部 transport。

第一條與 freepy 現況最相容。若需要 streaming reply，先保留 Python OpenAI SDK adapter。

### Windows

repo 已記錄：jpm compiler 預設、native DLL 被 LSP/REPL 鎖住、static library 打包、中文 argv 等摩擦；本次又觀察到 subprocess tests access violation。故 production Janet 目標應先定 Linux，Windows 只承諾開發／pure-core 測試，除非另開平台工作流。

### FFI

Janet 有 C FFI、native module、embedding，但錯誤型別與 ownership 可直接 segfault，callback 能力也有限。若只是接既有 Python，stdio/local HTTP 通常比穿 Python ABI 乾淨。

### Concurrency 差異

Janet fiber/channel 不是 Python `asyncio` 的同義詞：取消偏合作式；channel close、buffered data、跨 thread marshal 都有不同語意。agentloop 需以狀態機與 trace 重寫，不可逐行翻譯。

### Build reproducibility

repo 記錄 `jpm build` 不會可靠追蹤所有 import dependency。CI 應 clean build，否則可能測到舊 executable。

## 尚未證明

- FUSE 或 9P server／mount。
- Podman、cgroup、seccomp policy backend。
- HTTPS/SSE production transport。
- 高負載 benchmark、fuzz、長時間 durability。
- Windows subprocess 的穩定修復。

## 來源

- `langlab-janet/modules/llm-http/README.md`：llm-http 模組
- `langlab-janet/test/llm-http-server.janet`：tool-loop wire test
- `langlab-janet/modules/pi-shell/proc.janet`：pi-shell process
- `langlab-janet/docs/17-用-spork-http-打-api.md`：HTTP 限制
- `langlab-janet/docs/00b-windows-vscode.md`：Windows 筆記
- `langlab-janet/FINDINGS-踩坑.md`：踩坑紀錄

這些路徑屬於 2026-08-10 評估時的外部相鄰工作區，不包含在本 repository。
