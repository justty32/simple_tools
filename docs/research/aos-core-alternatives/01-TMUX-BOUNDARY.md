# aos-core 與 tmux 的真正關係

## 判斷

如果只看這條主路徑，aos-core 與 tmux 的內部 client/server 幾乎同構：

```text
shell
  -> 薄 client
  -> Unix socket
  -> 常駐 server
  -> 解析 argv／命令
  -> 執行
  -> 回傳輸出與完成狀態
```

但兩者的公共承諾不同：tmux 公開的是命令語言、CLI 與 control mode；aos-core 公開的是
「像一個 Unix process」的命令／插件契約。

## 內部能力對照

| aos-core | tmux 內部 |
|---|---|
| `request_start` 帶 argv、cwd | `MSG_COMMAND` + identify cwd/environment |
| stdin/stdout/stderr chunk frames | identify 階段傳 stdin/stdout fd，另有內部訊息 |
| registry 解析巢狀命令 | command parser + command queue |
| exit frame | `MSG_EXIT` 等完成訊息 |
| Unix socket daemon | tmux server socket |
| protocol version | `PROTOCOL_VERSION` |

tmux 的 [`tmux-protocol.h`](https://github.com/tmux/tmux/blob/master/tmux-protocol.h)
明確列出 `MSG_COMMAND`、identify、read/write 與 exit 類型，並要求修改 message data 時
bump protocol version。它的 [`client.c`](https://github.com/tmux/tmux/blob/master/client.c)
會打包 argv、傳 cwd、environment 以及 stdin/stdout file descriptors，再把命令送給 server。

所以「tmux 已經內建了類似功能」這個觀察是對的，但只是骨架相似，不代表 tmux 已經有
aos-core 的公共 plugin contract。

## tmux 不是完全沒有暴露

tmux 提供兩個正式入口：

1. `tmux <command> ...`：一次性 client，替使用者處理私有 client/server protocol。
2. `tmux -C`／`-CC` control mode：給外部程式使用的持久文字介面。

Control mode 允許程式從 stdin 傳 tmux commands，從 stdout 收 `%begin`、`%end`、
`%error`、pane output 與非同步通知。它最初就是為 iTerm2 這類外部 UI 設計的。

來源：

- [tmux Control Mode wiki](https://github.com/tmux/tmux/wiki/Control-Mode)
- [tmux(1) CONTROL MODE](https://man.openbsd.org/tmux#CONTROL_MODE)

因此準確說法不是「tmux 把能力藏起來」，而是：

> tmux 把穩定的自動化邊界放在命令文字與 control mode；底下的 imsg 協定和 C 結構
> 仍是沒有公共相容性承諾的實作細節。

## 為什麼不把底層協定直接公開

### 1. 內部協定假設兩端一起升級

tmux protocol 直接依賴 C 結構、native integer、imsg 與 Unix fd passing。這很適合同一份
tmux build 產生的 client/server，但不是天然的跨版本、跨平台公共協定。

實際發生 imsg 版本錯配時，上游維護者的處理方式仍是要求升級後完整重啟 tmux，而不是
承諾任意新舊 client/server 組合可互通：

- [Version skew issue #4356](https://github.com/tmux/tmux/issues/4356)

一旦把 wire protocol 變成產品，就要維護 capability negotiation、舊 message 解碼、錯誤語義、
跨版本測試與文件。這不是把 header 安裝到 `/usr/include` 就完成了。

### 2. tmux command 深度依賴內部物件

tmux command 會直接操作 client、session、window、pane、PTY、format tree、event loop 與
command queue。它們不是只有 stdin/argv/exit 的獨立函式。

若公開 in-process C plugin API，就要穩定：

- 內部結構與 ownership；
- libevent callback／非阻塞規則；
- client/session target 的生命週期；
- command queue 的等待、取消與錯誤傳遞；
- tmux 每次重構後的 ABI。

這個承諾會綁住 tmux 內部演進。

### 3. `.so` 插件會把所有 session 放在同一 crash boundary

第三方插件如果 segmentation fault、破壞 heap、deadlock 或長時間阻塞 event loop，整個 tmux
server 及其中所有 session 都會受影響。tmux 現有的 script/plugin 生態透過 tmux commands、
formats、hooks、user options 與 `run-shell` 擴充，刻意保留 process boundary。

### 4. 專案目標不同

tmux 是 terminal multiplexer，不是一般 command runtime。對外部整合而言，CLI + control mode
已經覆蓋主要需求；再維護一套通用 C plugin SDK 對 tmux 的核心用途收益有限。

## aos-core 與 tmux 仍有實質差異

`aos-core` 把一個命令的可見世界刻意壓到：

```text
argv + cwd + stdin + stdout + stderr + exit status
```

而且用「一條連線一條 thread」讓插件可以安全地從控制流角度阻塞。tmux 則是單一事件驅動
server，command 與 terminal/session state 緊密耦合。

因此 control mode 可以替代「控制 tmux」的那部分，不能替代：

- 任意新命令註冊；
- 通用 C/C++ module host；
- 插件自己的跨呼叫常駐狀態；
- 以 raw stdin/stdout/stderr 作為每個插件共同 ABI。

## 工程結論

不要直接實作 tmux 私有 protocol，也不要把 tmux fork 成 aos host。那會把 aos 綁到 tmux 的
內部版本與 event-loop 語義，維護成本高於保留目前的小核心。

如果 AOS 要管理 terminal/session，可以另外寫 control-mode adapter；如果要承載 agent、LLM
與工具插件，仍應保有自己的窄邊界。
