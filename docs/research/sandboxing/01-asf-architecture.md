# ASF 架構拆解

ASF 是一個宿主端 CLI，不是常駐 daemon。`sandbox.sh open <agent>` 讀取 runtime manifest，產生一次 session 的不可變計畫，建立 Podman 資源，驗證限制生效後才啟動 agent；結束時清除臨時容器、網路與 secret。

它限制的是「可執行 workload 擁有的能力」，不是嘗試理解模型的推理或意圖。

## 1. 檔案系統

容器預設看不到宿主檔案。ASF 只將 `repos.yml` 宣告的 repository bind-mount 到 `/workspace/repos/<name>`，每筆指定 `ro` 或 `rw`。

其他持久狀態使用 named volume；未宣告的容器內容在結束後丟棄。ASF 自己的 checkout 以唯讀方式掛載，`secrets/` 再覆蓋一層空的唯讀 tmpfs，避免宿主環境檔案因上層掛載而露出。

基礎設定見 [devcontainer.base.json](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/.devcontainer/devcontainer.base.json)，repo mount 的產生方式見 [devcontainer.py](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/asf/devcontainer.py)。

## 2. 程序與資源

runtime 使用 rootless Podman，容器內為非 root 使用者。不可關閉的核心限制包括：

- `--cap-drop=ALL`
- `--security-opt=no-new-privileges`
- 關閉 IPv4 與 IPv6 forwarding
- 不掛載 Podman socket

可選的資源限制包括 memory、CPU、PID 數量、open files、process 數量、core dump、private IPC，以及 `/tmp`、`/run` tmpfs。只有明確宣告時才能加 `NET_RAW`；agent runtime 不允許 `NET_ADMIN`。

實作位於 [config.py](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/asf/config.py)。這些限制中，CPU、memory、PID 主要由 cgroup 執行；capability、user namespace 與 `no-new-privileges` 負責降低提權面。

## 3. 三種網路模式

### isolated

agent 只加入 Podman `--internal` 網路，沒有直接對外路徑。它仍可能連到同網路的 LiteLLM broker，因此 isolated 的精確意思是「runtime 沒有直接外網」，不是「資料絕對無法離開」。

### proxy

```text
agent ── internal network ── Caddy ── egress network ── Internet
```

agent 只有 internal network；Caddy 同時加入 internal 與 egress，成為唯一出口。Caddy 設定由 manifest 自動產生：

- 只允許列出的 hostname。
- 阻擋 loopback、private、link-local、metadata 與特殊位址。
- 只接受 `CONNECT`，目的 port 限 443。
- 最後一條規則為 deny all。

因為 agent 沒有 default route，忽略 proxy 環境變數也無法直接出去。產生規則的程式位於 [proxy.py](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/asf/proxy.py)。

### routed

用於 HTTP proxy 無法承載的 TCP、UDP、ICMP 或掃描流量。agent 只取得 manifest 指定 CIDR 的 static route；獨立 gateway 用 nftables 綁定來源、目的、protocol 與 port。

`NET_ADMIN` 只短暫授予初始化 sidecar；規則載入後 sidecar 必須退出，長駐 gateway 沒有 capability。此模式較複雜且擴大攻擊面，ASF 建議一般 workload 優先使用 proxy。

完整拓樸見 [NETWORK-DESIGN.md](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/docs/NETWORK-DESIGN.md)。

## 4. Secret broker

啟用 LiteLLM broker 時，長效 provider API key 只掛入 broker，不進 agent。agent 得到 session token，並把 OpenAI/Anthropic base URL 指向 internal network 上的 broker。ASF 同時從 Caddy allowlist 移除 provider domain，降低 agent 找到其他 key 後直連的可能。

這不是資料外洩的完全防護：agent 傳給模型 provider 的 prompt 本來就會離開環境。broker 解決的是憑證暴露與集中管控，不是內容判斷。

## 5. 啟動驗證與清理

ASF 不只產生設定，還在 agent 啟動前用 throwaway container 驗證實際狀態：允許路徑、拒絕路徑、禁止 port、private address、無 default route、無直接 DNS 與 provider 旁路。阻擋測試失敗時 fail closed，不啟動 agent。

session 會保存 runtime plan、verification report 與 cleanup report；只有 manifest 宣告的 state volume 持久存在。這種「產生、實測、留下證據」是 ASF 最值得借用的部分。

## 6. 明確限制

- allowlist 限制目的地，不限制傳出的內容；允許 GitHub 就可能向 GitHub 外傳。
- 共用 CDN IP 會讓實際出口範圍比 hostname 看起來更寬。
- legitimate DNS resolver 仍可能形成低頻寬通道。
- 同一 workload 內的多個 logical agent 共享檔案、環境和憑證。
- broker 是受信任出口；isolated runtime 仍可透過 broker 傳資料。

因此 ASF 是 defense in depth，不是「執行任意惡意程式也絕對安全」的保證。其信任邊界與限制整理在 [TRUST.md](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/TRUST.md)。
