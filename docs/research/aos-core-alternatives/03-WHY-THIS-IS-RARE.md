# 為什麼這類通用功能少見

## 結論

安全問題是重要成本，但不是最主要原因。更根本的原因是這個功能落在一個窄交集，而且
Unix 與 RPC 生態已經提供三條更自然的分流：

```text
無狀態工作              -> 直接 exec 普通程序
需要常駐狀態            -> typed RPC service
需要第三方擴充與隔離    -> 外部 plugin process
```

`aos-core` 選擇把三者重新疊起來：表面仍像普通 Unix process，背後卻是保存狀態的 daemon，
而且插件直接進入 host process。只有同時需要 shell composability、低啟動成本、跨呼叫狀態與
native extension 時，這個組合才顯著優於上述分流。

## 不是沒人做，而是通常藏在具體產品裡

很多成熟工具都有薄 client／常駐 server：

- tmux：保存 terminal/session state；
- Docker CLI/daemon：保存 container control plane；
- ssh-agent、gpg-agent：保存 key 與授權狀態；
- clangd、rust-analyzer 等：保存索引與分析狀態；
- editor/plugin hosts：保存 buffer、UI 與 language runtime state；
- database CLI/server：保存資料與執行引擎。

它們沒有抽成同一個通用框架，是因為真正困難的語義都屬於各自領域：tmux 要處理 PTY，
Docker 要處理容器與權限，language server 要處理文件版本，database 要處理 transaction。
共用的 Unix socket 和 request dispatch 只佔很小一部分。

## 為什麼「功能很小」反而不容易形成市場

### 1. 直接 exec 已經夠便宜、夠標準

對大部分 CLI，啟動一個 process 的成本比引入 daemon、socket discovery、shutdown、log、upgrade
與 stale socket recovery 更低。Kernel 還免費提供：

- argv 邊界；
- stdin/stdout/stderr；
- exit status；
- signal、pipe、redirect、job control；
- shell 組合與既有除錯工具。

只有 process 啟動昂貴，或需要保存模型、索引、連線池、cache、queue 等狀態時，daemon 才有
明顯收益。此時開發者又常常直接定義領域 RPC，而不是繼續模擬普通 process。

### 2. Shell stream 與 typed RPC 的目標不同

Unix process contract 很通用，但語義弱：stdout 只是一串 bytes，client 不知道哪段是 progress、
result、warning 或 event。Typed RPC 則能表達欄位、錯誤、版本、取消與 capability。

若主要 caller 是程式或網路服務，typed RPC 通常更好；若主要 caller 是人與 shell，普通 CLI
通常已足夠。aos-core 的位置是要同時服務兩邊，這本來就是少數需求。

### 3. 通用核心真正難的是邊界，不是 socket

一個 prototype 很短；一個公共平台必須長期回答：

- client 斷線後，正在執行的命令是否取消？
- `Ctrl-C`、SIGTERM、terminal resize 如何傳遞？
- stdin 是 TTY、pipe 或 regular file 時語義是否相同？
- stdout/stderr 的相對順序是否保證？
- backpressure、慢 client、無限輸入與輸出上限怎麼處理？
- timeout 是 client policy、daemon policy 還是 plugin policy？
- cwd、environment、umask、locale 與 fd inheritance 如何凍結？
- daemon upgrade 時舊 client、舊 plugin 與在途 request 怎麼辦？
- plugin 可以 fork、開 thread、註冊 background worker 或阻塞多久？
- 命令名稱衝突、plugin dependency 與 unload 怎麼處理？
- 多使用者、ACL、audit、secret 與 privilege separation 怎麼做？

所以「能工作的核心」可以很小；「十年穩定的公共產品」並不小。

## 安全究竟是不是主因

### 目前定位下，安全不是致命問題

若條件固定為：

- per-user daemon；
- socket 權限 0600；
- client 與 daemon 是同一 UID；
- 所有插件都由同一位使用者信任並安裝；
- daemon 沒有比使用者更高的 OS 權限；

那麼能安裝 `.so` 的人原本就能執行任意程式。插件系統並沒有憑空給他更多主機權限。
在這個 trust model 裡，主要風險是穩定性與資料完整性，不是跨使用者提權。

### `.so` 插件仍不是隔離邊界

成功載入的原生插件和 host 共享：

- address space 與 heap；
- thread、signal handlers 與 process globals；
- 已開啟的 file descriptors；
- host 內存中的 token、設定與其他插件狀態；
- libc／C++ runtime 與動態連結器。

因此插件可以因 bug 或惡意行為造成：

- segmentation fault，帶走整個 daemon；
- heap corruption，破壞其他命令；
- deadlock 或無限阻塞；
- symbol／allocator／C++ ABI 衝突；
- 讀取 host process 的秘密；
- fork 或 thread 洩漏，讓 shutdown 失控。

「載入失敗就跳過」只能保護 `dlopen()`／ABI check 的啟動階段，不能隔離插件執行期 crash。

### 哪些條件會讓它變成真正的安全問題

只要出現下列任一項，就需要 process isolation、明確 authentication/authorization 與 OS sandbox：

- daemon 以 root 或更高權限執行；
- 多使用者共用同一 daemon；
- socket 暴露到網路或 container boundary 外；
- 允許下載不受信任的第三方插件；
- daemon 長期保存 API keys、signing keys 或其他 secrets；
- 插件能替較低權限 caller 執行 confused-deputy 操作。

此時應把插件改成獨立 process，host 只提供 capability-shaped RPC；再配合 uid、namespace、
seccomp、cgroup、filesystem/network policy。光把 `.so` ABI 換成 JSON 不會增加隔離。

## 應用面是不是太窄

歷史上是窄，但不是零。最適合 aos-core 形狀的工作負載通常同時有：

1. 高頻、短小的 CLI 呼叫；
2. 昂貴而值得保存的模型、索引、cache 或連線池；
3. 人仍需要從 shell 直接呼叫、pipe 與 redirect；
4. 插件由同一個開發團隊控制；
5. 本機低延遲比跨主機 interoperability 重要；
6. C/C++ native library 是主要能力來源。

過去這些條件很少同時成立。Agent 工具讓交集變大：模型與索引昂貴、工具呼叫密集、不同
能力要共享本機 runtime，同時工程師仍想用 shell 重現與除錯。

但 AI 生態的另一條路是 MCP：把工具定義成 JSON Schema + structured result。它更適合 LLM
caller，卻犧牲通用 Unix stream contract。因此 aos-core 是否有價值，取決於 AOS 要優先服務：

- **Linux process／shell 作為共同語言**，還是
- **AI tool schema 作為共同語言**。

兩者可以透過 adapter 共存，不必讓其中一個吞掉另一個。

## 對 aos-core 的具體邊界建議

### 可以保持小的前提

- 明寫「single-user、trusted plugins、local-only」；
- socket 預設 0600；
- 不承諾 hostile plugin isolation；
- 保持 plugin host API 極窄；
- 不把 terminal multiplexer、scheduler、sandbox、network RPC 全塞入核心；
- MCP、HTTP、LLM、tool registry 都做成上層 adapter/plugin。

### 進入公共插件生態前的最低門檻

1. 插件改成 process boundary；
2. protocol/ABI version negotiation；
3. cancellation、timeout、backpressure 與 resource budget；
4. capability/authorization model；
5. plugin crash、daemon restart 與 upgrade recovery；
6. 對 stdout/stderr ordering、disconnect 與 signal 的書面契約；
7. fuzzing、compatibility matrix 與惡意 client 測試。

## 最終判斷

這類功能少見，不是因為「Unix socket daemon 做不到」，而是因為它位於三個成熟模型之間，
每個模型都已經能更自然地解決大部分需求。aos-core 的價值正是那個剩下的小交集。

若它維持為 AOS 自己的 trusted local substrate，小而專用是優點；若要成為第三方通用平台，
安全、生命週期與相容性會迅速讓它不再是一個小專案。
