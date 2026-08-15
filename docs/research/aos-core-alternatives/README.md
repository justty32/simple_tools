# aos-core、tmux 與開源替代方案調查

- 調查日期：2026-08-15
- 本機 `aos-core` 快照：`e9712f9`
- 本機 tmux：3.7b
- 範圍：本機單使用者 command daemon、Unix 串流、命令註冊與原生插件

## 結論先講

沒有一個成熟開源專案可以原樣取代 `aos-core`。最接近的是 OpenWrt 的
**ubus + rpcd**，但它的公共介面是 object/method + JSON，不是 Unix 程序的
`argv + stdin/stdout/stderr + exit status`；若要保留目前的 shell 語義，仍然需要
自訂 CLI 與串流 adapter。

`aos-core` 並沒有發明新的底層技術。tmux、Docker、SSH agent、GPG agent、編譯器
daemon 與各種 language server 都有「薄 client → 常駐 server」這個骨架。少見的是把
下列三件事同時做成一個通用、公開而且很窄的契約：

1. 常駐 daemon 與跨呼叫狀態；
2. 完整保留 Unix 命令的三條串流、argv 與 exit status；
3. 允許 C/C++ 原生插件直接註冊新命令。

這個交集過去不大。無狀態工作通常直接 `exec`；有狀態服務通常改用 typed RPC；
第三方插件則愈來愈傾向獨立 process，以免插件 crash 或相依衝突拖垮 host。

## aos-core 真正提供的契約

本輪不是拿「有 Unix socket」就算替代品，而是按下列能力逐項比較：

| 能力 | aos-core 現況 |
|---|---|
| 本機常駐服務 | Unix Domain Socket daemon |
| 呼叫形狀 | 保留 argv 邊界與 cwd |
| 資料面 | binary-safe stdin/stdout/stderr 串流 |
| 完成語義 | POSIX-style exit status |
| 派發 | 可探索的巢狀命令樹 |
| 擴充 | 編進來的模組或啟動時 `dlopen()` 的 `.so` |
| 插件介面 | 很窄的 blocking C ABI |
| 併發 | 一條連線一條 thread；插件可以阻塞 |
| 狀態 | daemon、模組與插件可跨呼叫保存狀態 |
| 相依 | C99、零第三方 runtime dependency |

任何缺少 raw stream、exit status 或插件註冊的方案，都只能替代其中一層。

## 建議

### 現階段

保留 `aos-core`。它約兩千多行有效程式，現在的責任邊界比導入 ubus/rpcd、D-Bus
或 gRPC 後的整合面更小。不要只為了「協定不是自己寫的」而換掉一個已經很窄、
可測且零相依的核心。

### 若需求改變

| 新需求 | 建議方向 |
|---|---|
| 目標本來就是 OpenWrt／embedded Linux | 用 ubus + rpcd |
| 願意放棄 shell contract，改成 typed local RPC | 用 Varlink／`sd-varlink` |
| 第三方插件不可信，必須 crash isolation | 改成外部 process；參考 HashiCorp go-plugin |
| 主要入口是 AI agent tools | 在 aos-core 上加 MCP adapter，不用 MCP 取代底層 |
| 要跨主機、跨語言與大型 streaming RPC | 才考慮 gRPC |

若未來真的要開放第三方插件市場，第一個架構改動應是**插件 process isolation**，
不是換 wire format。現在的 `.so` ABI 適合 trusted extensions；它不是安全隔離邊界。

## 文件導覽

- [aos-core 與 tmux 的真正關係](01-TMUX-BOUNDARY.md)
- [開源替代方案逐項比較](02-OPEN-SOURCE-ALTERNATIVES.md)
- [為什麼通用方案少見，以及安全邊界](03-WHY-THIS-IS-RARE.md)

## 一句話判斷

`aos-core` 是一個過去通常藏在具體產品內部、很少被單獨產品化的抽象；它的市場窄，
但在「高頻 agent 工具 + 常駐資源 + shell composability」出現後，這個交集可能正在變大。
