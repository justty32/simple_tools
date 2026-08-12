# 建議架構與落地順序

## 決策

以「一個 agent 實例一個 rootless Podman container」作為正式安全邊界；`agentloop` 保留回合、token、工具次數等邏輯預算，不負責 OS sandbox。

不要只包住 `run_shell`。如果整個 agent 仍在宿主 process，Python tools、LLM client、動態載入的程式碼仍保有宿主權限。只有確認所有 in-process 工具都是可信任程式碼時，才適合採用「只 sandbox 外部命令」的較輕方案。

## 建議元件

```text
freepy supervisor（可信任）
  ├─ SandboxPolicy：mount、network、resource、env
  ├─ Runtime：create / start / stop / inspect
  ├─ Secret broker：持有長效 API key
  └─ evidence：effective policy、exit reason、cleanup result
                        │
                        ▼
agent runtime（不可信任）
  ├─ agentloop + tools
  ├─ /workspace/task        rw
  ├─ /workspace/reference/* ro
  ├─ read-only root + tmpfs
  └─ none / internal-only network
```

建議新增獨立的 `runtime` 或 `sandbox` package。它的 API 可以接受 immutable policy，但不應讓模型透過 tool call 修改自己的 policy。

## 最小可用版本

第一版只支援：

- Linux／Manjaro 上的 rootless Podman。
- 一個 task workspace `rw`，零到多個 reference mount `ro`。
- `--network=none`。
- read-only root、非 root user、drop all capabilities、no-new-privileges。
- memory、CPU、PID、timeout 與 core dump 限制。
- 宿主 supervisor 負責停止、清理與記錄 exit status。

概念命令如下，實作時應使用 argv list 呼叫 Podman，不經 shell：

```sh
podman run --rm \
  --network=none \
  --read-only \
  --cap-drop=ALL \
  --security-opt=no-new-privileges \
  --userns=keep-id \
  --pids-limit=64 \
  --memory=1g \
  --cpus=1 \
  --ipc=private \
  --tmpfs /tmp:rw,nosuid,nodev,size=256m \
  --mount type=bind,src=/host/task,dst=/workspace/task,rw \
  --mount type=bind,src=/host/reference,dst=/workspace/reference,ro \
  freepy-agent:dev python -m worker
```

Podman 選項的正式語意應以 [podman-run](https://docs.podman.io/en/stable/markdown/podman-run.1.html) 為準。

## 需要 LLM 或有限網路時

第二版可借用 ASF 的 topology：

1. 建立每 session 專用的 `--internal` network。
2. agent 只加入 internal network，沒有 default egress。
3. LiteLLM broker 同時加入 internal 與 provider network；長效 key 只進 broker。
4. agent 只拿 session token。
5. 若還要下載套件或呼叫 Web API，再加入雙網卡 Caddy proxy 與完整 domain allowlist。

不要把 `HTTP_PROXY` 當邊界。必須從 agent container 內驗證不存在 default route、禁止目的地確實失敗、允許目的地確實經過 proxy。Podman `--internal` 的行為見 [podman-network-create](https://docs.podman.io/en/stable/markdown/podman-network-create.1.html)。

## 啟動與清理順序

順序屬於安全設計的一部分：

```text
validate policy
→ create networks/volumes
→ start broker/proxy
→ verify effective deny/allow behavior
→ start agent
→ collect result
→ stop all owned resources
→ record cleanup evidence
```

若驗證不明確，不應偷偷退回 unrestricted mode。安全相關 deny check 應 fail closed；外部服務暫時不可用則要與「policy 沒擋住」分開報告。

## 不應掛入容器的東西

- 整個 home directory 或 workspace root。
- Podman／Docker socket。
- 桌面環境共用的 SSH agent。
- 長效 provider、GitHub 或 cloud API key。
- 不需要的 Windows drive、WSL interop socket 或宿主 IPC。

需要 Git push 時，較安全的預設是容器內 commit、宿主 review 後 push；若一定要從容器 push，只提供單一用途、權限受限的 deploy credential。

## `bwrap` 與 Windows

若需求只是「執行一條無網路、只看某些目錄的 Linux 命令」，Bubblewrap 啟動更輕，可建立空 mount namespace、唯讀 bind、PID 與 network namespace；參考 [Bubblewrap README](https://github.com/containers/bubblewrap)。但長時間 agent、持久狀態、broker 與多網路拓樸更適合 Podman。

本專案的 shell 已明確以 POSIX／Linux 為前提，主要環境文件也指向 Manjaro 與 WSL。因此第一版不建議同時實作 Windows Restricted Token、Job Object 或 AppContainer backend。Windows 開發時可在 WSL 內使用 Podman，但 WSL 本身不是每 agent 的隔離邊界，仍應只 mount 必要路徑。

## 實作里程碑

1. `SandboxPolicy`、Podman argv renderer 與純單元測試。
2. offline container：mount、identity、resource、network-none 測試。
3. supervisor lifecycle：timeout、kill whole container、cleanup evidence。
4. 將完整 agent worker 移入 runtime；定義窄 IPC。
5. internal LiteLLM broker 與短效 session token。
6. 有實際需求後才加入 Caddy allowlist、啟動探針與使用紀錄。

第一至第三步完成後，就已有比 `rlimit` 更完整且可誠實描述的安全邊界；第四步完成後，才可宣稱每個 agent 實例彼此隔離。
