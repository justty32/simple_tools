# 開源替代方案逐項比較

## 摘要

最接近完整形狀的是 **OpenWrt ubus + rpcd**，最適合重新定義成 typed local RPC 的是
**Varlink**，最適合把不可信插件移出 host process 的是 **HashiCorp go-plugin**。

沒有方案同時保留 aos-core 現有的 blocking C ABI、binary shell streams、POSIX exit status、
跨呼叫狀態與零相依。

## 總表

| 方案 | daemon/socket | 動態擴充 | raw stdio | 持久狀態 | crash isolation | 主要代價 |
|---|---|---|---|---|---|---|
| ubus + rpcd | 有 | `.so` + executable plugin | 部分；公共 CLI 是 JSON | `.so` 有 | executable 有 | OpenWrt 相依與 event loop |
| Varlink | 有 | 服務自行實作 | 多回覆／upgrade，不是現成 shell contract | 有 | 依部署 | 沒有 plugin host |
| go-plugin | 插件有 RPC | 外部 plugin process | 可用額外 stream | 有 | 有 | host 最適合用 Go |
| D-Bus／sd-bus | 有 | 獨立服務 | 可傳 Unix FD | 有 | 有 | 複雜、語義偏 object RPC |
| gRPC | 有 | 自己建 host | 雙向 streaming | 有 | 依部署 | C/C++ 相依與 codegen 很重 |
| MCP | stdio／HTTP | tool servers | 結構化 content，不是 POSIX streams | 可做 | 通常有 | 是 AI tool protocol，不是 shell ABI |
| Valkey modules | 有 | in-process module | RESP request/reply | 有 | 無 | 本質是資料庫，語義不合 |

## 1. OpenWrt ubus + rpcd

### 它提供什麼

`ubusd` 是中央 message router；服務在執行期註冊 object/method，client 經 Unix socket 呼叫。
標準 `ubus` CLI 可列出 object、呼叫 method、監聽 event 與訂閱通知。

- [OpenWrt ubus 技術文件](https://openwrt.org/docs/techref/ubus)
- [ubus CLI source](https://github.com/openwrt/ubus/blob/master/cli.c)

`rpcd` 則是直接針對「不值得每個功能都寫一個 daemon」而做的小型 plugin host：

- 啟動時 `dlopen()` 指定目錄下的 `.so`；
- library plugin 的 `init()` 拿到 `ubus_context`，可以註冊方法；
- executable plugin 可先回報 method signature，之後由 rpcd fork/exec 呼叫；
- host 有 child stdin/stdout/stderr callback 與完成 callback。

來源：

- [rpcd 官方說明](https://openwrt.org/docs/techref/rpcd)
- [rpcd plugin API](https://lxr.openwrt.org/source/rpcd/include/rpcd/plugin.h)
- [rpcd plugin loader](https://lxr.openwrt.org/source/rpcd/plugin.c)
- [rpcd process execution](https://lxr.openwrt.org/source/rpcd/exec.c)

授權也友善：ubus 是 LGPL-2.1；rpcd 是 ISC。

### 為什麼仍不是 drop-in

標準 `ubus call object method '{...}'` 收送的是結構化 blob/JSON。它不會像 `aos` 一樣自動：

- 把 client stdin 原樣持續送給 method；
- 分離任意 binary stdout 與 stderr；
- 保留 POSIX exit status；
- 傳遞每次呼叫的 argv/cwd shell contract。

`libubus` 有 FD passing API，因此可以寫自訂 adapter，把 socketpair 或其他 fd 傳給服務；但
stock `ubus` CLI 沒暴露成通用 pipe 行為。補上這層後仍需維護自己的 CLI contract。

`rpcd` 還依賴 libubox、ubus、json-c、libuci、crypt 與 `dl`；它的核心是 `uloop` event loop，
library plugin 不能假設可以任意長時間阻塞。可執行插件有 isolation，但每次呼叫會 fork/exec，
也失去「同一 plugin process 自然保存狀態」的特性。

- [rpcd CMake dependencies](https://lxr.openwrt.org/source/rpcd/CMakeLists.txt)

### 適用判斷

- OpenWrt／embedded Linux：優先採用，避免另造平台既有的 bus。
- 一般 Linux 桌面：只有願意接受 JSON RPC 與這組 dependencies 時才值得。
- 要完整保留 shell streams：不是直接替換。

## 2. Varlink／sd-varlink

Varlink 是一個簡單、可自我描述的 typed RPC protocol：interface 定義 method、input/output、
error；message 是 NUL 結尾 JSON，常用 Unix socket transport。

它支援：

- 每個服務的 interface discovery；
- 一次 call 多次 reply，適合監看與 chunked result；
- `upgrade`：確認後把同一條連線交給自訂 protocol；
- socket activation；
- `varlinkctl` CLI。

來源：

- [Varlink protocol overview](https://varlink.org/)
- [Interface Definition](https://varlink.org/Interface-Definition.html)
- [Method Call、more 與 upgrade](https://varlink.org/Method-Call.html)

Varlink 可以替掉 aos-core 的 framing、typed metadata、錯誤與 discovery，但不提供 command tree
plugin host。若拿 `upgrade` 實作 raw stdin/out/err，資料面的自訂協定仍由 AOS 自己負責。

適合願意把 API 改成 typed methods、主要跑在新 Linux/systemd 環境的版本；不適合要求現有
shell contract 完全不變的 drop-in migration。

## 3. HashiCorp go-plugin

`go-plugin` 把插件做成獨立 process，透過 `net/rpc` 或 gRPC 溝通。它已長期用於 Terraform、
Vault、Nomad、Packer 等工具，採 MPL-2.0。

重要能力：

- plugin crash 不直接 crash host；
- versioned plugin sets；
- bidirectional callbacks；
- 額外 connection/multiplexing，可承載 `io.Reader/Writer` 類資料；
- gRPC 跨語言插件；
- reattach，允許 host 更新後重新連既有 plugin。

來源：

- [HashiCorp go-plugin](https://github.com/hashicorp/go-plugin)
- [go-plugin license](https://github.com/hashicorp/go-plugin/blob/main/LICENSE)

它解的是 plugin process protocol，不是完整 CLI daemon。若採用，仍要自己提供 aos command
registry、外部 CLI 與 shell stream mapping；而且 host 端以 Go 寫最自然。對純 C99 核心來說，
整體替換成本高，但它是未來做 untrusted plugin isolation 最值得參考的架構。

## 4. D-Bus／sd-bus

D-Bus 有成熟的 bus daemon、method/signal/object model、activation、introspection、ACL 與 Unix
FD passing。它比自訂 Unix socket protocol 更穩定，也有 C/C++ 與多語言 bindings。

- [D-Bus specification](https://dbus.freedesktop.org/doc/dbus-specification.html)

但 D-Bus 的自然介面是 typed method call，不是任意 binary stdin/stdout/stderr。用 FD passing
可以模擬 shell stream，代價是仍需自訂高階契約。若 AOS 要整合 desktop/system services，D-Bus
合理；若只是 per-user agent command daemon，複雜度偏高。

## 5. gRPC

gRPC 是成熟、跨語言、具 code generation 的 RPC 框架；HTTP/2 原生支援雙向 streaming。

- [gRPC overview](https://grpc.io/about/)
- [C++ streaming guidance](https://grpc.io/docs/languages/cpp/best_practices/#streaming-rpcs)

它能完整建模 stdin、stdout、stderr、control 與 exit event，但不提供 plugin discovery/host；這些
仍要自己設計。對目前「零相依、C99、同機少量 client」的工作負載，gRPC C/C++ dependency、
protobuf codegen 與 async lifecycle 明顯過重。只有跨主機／跨語言成為核心需求時才值得。

## 6. MCP

MCP 是 AI 工具層協定：server 發布工具名稱、說明與 JSON Schema；client discovery 後以
structured arguments 呼叫，結果是 text/image/resource/structured content 或 tool error。
官方 SDK 支援 stdio 與 HTTP transports。

來源：

- [MCP TypeScript tools](https://github.com/modelcontextprotocol/typescript-sdk/blob/main/docs/servers/tools.md)
- [MCP Rust SDK](https://github.com/modelcontextprotocol/rust-sdk)

它不等價於 POSIX process：沒有共同的 argv/cwd/raw stdin/out/err/exit contract。對 AOS 最合理的
位置是 adapter：把 aos commands/tool plugins 發布成 MCP tools，或讓 aos plugin 呼叫 MCP
servers；不用 MCP 取代底層 command daemon。

## 7. 為什麼不建議 Valkey／Redis module 路線

Valkey 有 daemon、CLI、Unix socket、跨呼叫狀態與 `.so` module API，看起來也很接近；但它的
公共語義是 RESP database commands 與 key/value state，不是 process invocation。Blocking module
command 還要遵循 server event-loop 與 blocked-client API。

- [Valkey project](https://github.com/valkey-io/valkey)
- [Valkey modules API](https://valkey.io/topics/modules-api-ref/)

為了 command host 而帶入完整資料庫、replication、persistence、ACL 與 module lifecycle 是
錯誤抽象；除非 AOS 的核心狀態本來就要成為 Valkey data model，否則不採用。

## 最終排序

1. **保持目前語義：保留 aos-core。**
2. **接受 JSON/object RPC：ubus + rpcd。**
3. **接受 typed interface 且偏新 Linux：Varlink。**
4. **重視第三方插件隔離：採 go-plugin 的 process 架構，不必照搬 Go API。**
5. **AI 工具相容：新增 MCP adapter。**
6. **跨主機大系統：gRPC。**
