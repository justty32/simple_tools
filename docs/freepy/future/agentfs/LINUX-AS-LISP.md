# Linux-as-Lisp 設計透鏡

這是設計隱喻，不是說 Linux 在語言分類上真的是 Lisp。值得借用的是：少量通用原語、名稱解析、環境、組合與「資料可再次被解釋」；不值得照抄的是無型別文字協定和到處帶副作用的寫檔介面。

這份保留 agentfs 的 namespace 透鏡；慢速機率求值、Goal、OS resource controller 與 Lisp
condition/restart 已升格到 [Agent Machine](../agent-machine/README.md) 的
[Plan 9／Lisp 規格](../agent-machine/PLAN9-LISP.md)。

跨 agentfs、九軸、記憶與時間尺度的綜合版本見 [Agent World 設計報告](../../../design/agent-world/README.md)。

## 對應關係

| Lisp／求值概念 | Linux／Unix 機制 | agent 系統中的含義 |
|---|---|---|
| symbol | path | agent、工具、記憶與資源的穩定名稱 |
| environment | per-process namespace | 每個 agent 看見的世界 |
| binding | mount／bind mount | 把能力或資料注入某 agent 的視野 |
| lexical scope | namespace subtree | 預設可組織地尋址，不等於自動授權 |
| value/reference | file content／fd | 可重找的名稱與已解析的 live capability |
| evaluator | process | 帶著上下文、限制與生命週期執行動作的 agent |
| function composition | pipe | 小工具串成資料流，而非一個萬能工具 |
| quote | 把內容固定成檔案／object | context 段落提升為不可變記憶 |
| unquote／dereference | read／load | 需要時把連結內容拉回工作 context |
| dynamic binding | 私有 mount view | 同一路徑在不同 agent 可解析成不同能力 |
| fork + evaluation | fork／clone + exec | 複製受限環境，再讓 child 執行指定角色 |
| evaluation trace | process／I/O history | Round、Step、tool call 與事件紀錄 |

更精確地說，agent 並不是「檔案」；agent 是在某個 namespace 裡求值的 process。agentfs 是它可觀察、可組合的世界，而 open 後得到的 handle 才像已解析且帶權限的引用。

## 每個 agent 有自己的世界

canonical identity 仍是唯一組織路徑，例如 `/root/research/web`；但 agent 真正使用的是 supervisor 為它組出的私有 view：

```text
/self       -> /root/research/web/.agent
/parent     -> 經權限裁切的 /root/research/.agent
/children   -> 自己管理的直屬 child views
/team       -> 本任務獲准看見的成員與服務
/tools      -> 授予的工具 file protocols
/memory     -> 這個 agent 可讀的組織化記憶
/work       -> workspace 的受限 view
/srv        -> 可 attach 的命名服務
```

這使權限可以被理解成「什麼被掛進我的世界」，而不只是每次操作再查一張巨大 allowlist。server 仍須在 open/read/write 時重新驗證 grant；namespace 是最小暴露面，不是唯一安全邊界。

同一份記憶可被掛到多個分類路徑而不複製內容，例如一項設計決策同時出現在 `/memory/decisions/`、`/memory/topics/sandbox/` 與某個 task 目錄。分類是 view，content-addressed object 才是本體。

## Context 也成為 namespace

模型輸入可以看成一次求值所需的 environment，而非無限增長的聊天字串：

```text
/self/context/
  manifest.json       本步實際工作集及順序
  instructions.md     必須保持 inline 的有效指令
  working/            目前載入的段落
  links/              尚未展開的記憶引用
  tool-results/       本 Round 需要的工具結果
```

把一段舊內容提升成 memory object，近似 `quote`：內容暫時不求值，只保留名稱、摘要、來源與 hash。`memory_load` 近似受預算和權限控制的 dereference。詳見 [組織化 context 規格](../memory-tools/ORGANIZED-CONTEXT.md)。

這會自然產生一個「context compiler」：它依目前指令、task、權限、token budget 和 pinned refs，編譯出下一次 `ask()` 的有序工作集。它不替模型做決策，只處理表示、選頁與來源標記。

## Linux 帶來的額外啟發

- VFS：不同 provider 應服從同一組 `walk/list/stat/read` 基本語意；provider 不等於 transport。
- `/proc`、`sysfs`：live object 可以投影成小檔案，但 projection 不應假裝是持久資料。
- `configfs`：若生命週期由 `mkdir/rmdir/write` 驅動，必須明說並提供 commit/rollback；不能和唯讀 projection 混淆。
- mount namespace：每個 agent 應有不同 view；共同全域樹容易洩漏名稱和結構。
- cgroup v2：資源是父到子的階層式分配；child 只能進一步收緊，不能憑路徑擴權。
- fd：名稱查找和已開啟 handle 要分開。handle 綁 actor、instance、grant generation，避免只靠可猜 path。
- seccomp：即使 namespace 沒暴露某路徑，execution backend 仍要縮減實際 syscall／network／mount 攻擊面。

## 不要過度 Lisp 化

Linux 介面不是 homoiconic AST；一堆文字檔也不會自動得到良好語意。agentfs 應避免：

- 讓 path 同時表示 identity、authority 與可執行命令。
- 將自然語言寫入 `ctl` 後由模型自由猜測操作。
- 依目錄名稱推斷資料型別；每個節點仍需 schema、mime、version。
- 把載入的記憶當成高權限指令。memory 是帶 provenance 的資料，不能提升原始 authority。
- 讓一個 link 隱式遞迴展開整棵樹；每次 dereference 都要有 depth 與 bytes budget。

最小核心仍應是 typed provider：path 負責尋址，handle 負責能力，schema 負責語意，supervisor 負責狀態轉移。檔案介面讓這些東西可組合，但不能抹掉它們的差別。

## 可採用的句法

用這組語彙檢查設計會很實用：

```text
組織 = canonical namespace
agent view = evaluation environment
授權 = mount capability
委派 = bind 一個受限 view
spawn = 建立 child evaluator
Round = 一次有邊界的求值活動
Step = 一個 ask -> message reduction
記憶提升 = quote to object
記憶載入 = checked dereference
context compact = 重編譯 working set，不刪原始 trace
```

## 參考

- [Linux VFS](https://docs.kernel.org/filesystems/vfs.html)
- [Linux namespaces](https://man7.org/linux/man-pages/man7/namespaces.7.html)
- [mount namespaces](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html)
- [sysfs](https://docs.kernel.org/filesystems/sysfs.html)
- [configfs](https://docs.kernel.org/filesystems/configfs.html)
- [cgroup v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)
- [seccomp BPF](https://docs.kernel.org/userspace-api/seccomp_filter.html)
