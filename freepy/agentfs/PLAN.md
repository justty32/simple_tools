# agentfs 規劃

**這份是實作規格，尚未寫程式。** `agentfs` 把正在運作的 agent、組織與 runtime 狀態投影成一棵 synthetic filesystem；概念接近 Plan 9 process files、Linux `/proc` 與 `/sys`。

它在整體 machine 中只負責 namespace projection；process、resource、goal 與 condition/restart
邊界見 [Agent Machine](../agent_machine/README.md) 與
[Plan 9／Lisp 規格](../agent_machine/PLAN9-LISP.md)。

## 一句話

```text
agent path 既是組織身分，也是查詢 live state 的 namespace
```

例如 `/root/build/tests` 是 agent identity；它的狀態可從同一位置下的虛擬檔案讀取。

## 理想模型：Plan 9

Plan 9 不是表面上的路徑命名靈感，而是這層的長期設計方向：

> 每個 agent 是一個有身分、有資源、有權限、可通訊的 file server；agent group 是由這些服務組成、可依權限裁切與掛載的 namespace。

對應原則：

- 狀態表現成檔案，組織表現成目錄樹。
- `read` 是查詢；若將來開放 `ctl`，`write` 是送命令，不是改資料庫欄位。
- mount/view 決定一個 agent 看得到哪些其他 agent 與能力。
- 大型資料與記憶以 link/ref 連接，不複製進每個 context。
- communication、runtime、team、memory 各自提供小型 file protocol，再由 namespace 組合。
- 後端可以從 Python resolver 換成 FUSE、9P 或遠端 file server；穩定契約是 path、file content 與權限語意。

這個方向不要求第一版立刻實作完整 9P。先讓內部 provider 真正符合 `attach → walk → open/read/stat` 的思考方式，之後換 transport 才不會只是把普通 REST/JSON API 偽裝成檔案。

Linux 的 VFS、namespace、fd 與 pseudo-filesystem 還能被當成一套「Lisp 式求值環境」來理解；完整對應與限制見 [Linux-as-Lisp 設計透鏡](LINUX-AS-LISP.md)。

## Namespace

metadata 全放在保留目錄 `.agent/`，child agent 仍直接出現在父路徑下：

```text
/root/
├─ .agent/
│  ├─ state
│  ├─ round
│  ├─ step
│  ├─ env/<public-name>
│  ├─ resources/<name>
│  ├─ permissions/{tools,engines,network,mounts}
│  ├─ tasks/<task-id>/{status,brief,report}
│  ├─ snapshot.json
│  └─ generation
├─ research/
│  ├─ .agent/...
│  └─ web/.agent/...
└─ build/.agent/...
```

所以使用方式接近：

```sh
cat /root/build/.agent/env/model
cat /root/build/.agent/resources/tokens
cat /root/build/tests/.agent/state
```

不用直接做 `/root/build/env/...`，因為 agent child 也可能叫 `env`、`status` 或 `tasks`；單一保留的 `.agent` 可以永久隔開「組織子樹」與「agent 自己的檔案介面」。`agent_identity.py` 禁止 child segment 恰好為 `.agent`。

這些 `/root/...` 是 logical paths，不是宿主根目錄。FUSE/9P mount 可以讓它看起來像真路徑，但 resolver 永遠先用 canonical agent segments 查 namespace，不能拿字串直接交給 `open()`。

## 它不是另一份資料庫

authoritative state 仍由原本各層擁有：

| 路徑 | 提供者 |
|---|---|
| `state`、`round`、`step` | agent_runtime / agentloop Handle |
| `env/` | runtime 宣告的 public variables |
| `resources/` | runtime budget pool 的 effective/reserved/used |
| `permissions/` | runtime effective policy，不是 request policy |
| `tasks/` | team_tools task/report store |
| child directories | team member/runtime registry |

agentfs 只做 projection。不能讓人直接改底層 JSON，再期待資源、container 或 task state 自動跟著變；那會繞過所有 invariant。

`snapshot.json` 提供一次讀取所需的完整、同 generation 視圖。逐檔讀取可能在兩次 open 之間改變，因此每個檔都能附 generation；需要一致性就讀 snapshot，不承諾整棵目錄 walk 是 transaction。

## `env` 不是 `os.environ`

絕不原樣暴露 process environment。`env/` 只包含 manifest 明確標成 public/introspectable 的虛擬變數，例如 model alias、workspace label、locale。

- secret value 不出現；預設連名稱都隱藏。
- brokered credential 可視需要只顯示 `brokered`，不能顯示 token。
- 宿主 PATH、HOME、SSH_AUTH_SOCK、proxy token 等不因存在於 process 就自動匯出。
- 每個值有 bytes 上限，內容為 UTF-8 text；binary 用 artifact/memory ref。

這樣 `env/somevar` 表示 agent 對外公開的 state variable，而不是 `/proc/<pid>/environ` 的任意鏡像。

## 讀取者權限

同一路徑對不同 actor 可以看到不同內容：

- agent 自己可看非秘密的 effective state。
- ancestor 只有持有 `inspect_descendant` grant 才可看 descendant 詳情。
- peer 預設只能看到 public presence，不能看 env、task brief、mount 或用量。
- team task/artifact 仍套 task scope；知道 path 不等於獲准讀取。
- host operator 可有獨立 audit view，不冒充 `/root` agent。

因此不能把一份共同、可直接 bind-mount 的實體目錄交給所有 agent。正式 mount 必須是 per-view session：server 從 supervisor 綁定 actor path + instance id，不能相信 client 在請求裡自稱是誰。

## 第一版介面

先做 resolver 與模型工具，不急著碰 FUSE：

```python
agent_list(path="/root/build")
agent_read(path="/root/build/.agent/resources/tokens")
```

兩者只讀、有限制筆數與 bytes，錯誤回字串。這能先驗 namespace、provider、權限與 snapshot 語意，也能在 Windows 測試。

`introspection_tools.self_status()` 與 agentfs 不重複：前者把「我現在怎樣」壓成一次呼叫可讀完的摘要，省 context；agentfs 提供細粒度、可列目錄、可跨 agent 查詢的通用 namespace。兩者應共用同一批 read-only providers，不能各算一份 effective state。

第二版再把同一個 `list/read/stat` provider 接到：

- Linux FUSE：本機使用方便，但 container 需要審慎處理 `/dev/fuse`。
- 9P server：最符合「檔案伺服器」模型，可做 per-session namespace；mount 與 authentication 成本較高。
- materialized snapshot：只供診斷／測試，不是 live control plane，也不得回寫。

## 寫入與控制

v1 全部 read-only。將來若要 Plan 9 風格控制，可新增單一 `.agent/ctl`：

```sh
echo 'pause' > /root/build/.agent/ctl
echo 'stop reason=operator' > /root/build/tests/.agent/ctl
```

server 必須把命令轉成現有 typed supervisor/team operation，再做相同 grant、scope、instance id、idempotency 與 audit 檢查。`resources/tokens`、`permissions/tools`、`tasks/*/status` 等 projection files 永遠不能直接 write；否則寫檔會繞過 reserve、grant subset 與 task transition。

communication 的 inbox 也先不映射成可寫檔案。`send_message` 保持 typed operation；之後若加入 `.agent/inbox`，寫入必須仍走相同 envelope 驗證與原子投信。

## 與 Round／工具輸入的關係

`state` 可顯示 `thinking`、`running_tool`、`paused`、`completed`；`round` 與 `step`
是小文字檔。讀 agentfs 不會自動啟動或延長對方 Round。

若 supervisor 決定監看檔案變化並把事件注入 Handle，那是獨立 subscription/control policy。v1 沒有阻塞 `read` 或「等狀態改變」工具。

## 實作順序與關卡

1. 在 `agent_identity.py` 保留 `.agent` segment。
2. `path.py`：拆 agent prefix、metadata path、fragment，拒絕 `..` 與非 canonical path。
3. `providers.py`：runtime、loop、team 的 read-only adapters。
4. `view.py`：依 actor/effective grants 過濾 list/read/stat。
5. `snapshot.py`：generation 與單檔一致 snapshot。
6. `tools.py`：`agent_list`、`agent_read`。
7. 真需求出現後才選 FUSE 或 9P adapter。

必測：`.agent` child collision、segment-prefix 陷阱、self/ancestor/peer 不同視圖、secret 不出現、舊 instance 不能讀新 instance private state、generation 一致、bytes/list 上限、provider error fail closed、任何 projection file 都不可直接 mutation。
