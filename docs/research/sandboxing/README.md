# 程式實例存取限制調查

- 調查日期：2026-08-10
- ASF 版本：1.0，commit `e120daf6a8db573760cdac1b1b2c8bb162fd1186`

## 結論

要限制一個會自行呼叫工具、啟動子行程的程式實例，可靠的方法不是分析它想做什麼，也不是維護危險指令黑名單，而是讓它從一開始就在作業系統建立的邊界內執行。

對本專案，建議採用以下模型：

```text
可信任的宿主 supervisor
  - 決定 policy、生命週期與預算
  - 保管長效憑證
               │ 狹窄 IPC
               ▼
每個 agent 一個 rootless Podman container
  - 只掛載獲准的 workspace
  - CPU / RAM / PID 有上限
  - 無網路，或只能經過 broker / allowlist proxy
```

若程式已經在宿主上執行，最安全的做法是停止後在 sandbox 內重新啟動。CPU、RAM 等資源有時能事後納入 cgroup，但檔案視野、使用者與網路 namespace 不應依賴事後補套。

## 最重要的原理

限制必須放在受限程式改不到的位置。

- `cwd=/workspace` 只指定起始目錄，不會阻止程式讀取其他路徑。
- `shell=False` 防止 shell 二次解析參數，不會縮小檔案或網路權限。
- `HTTP_PROXY` 只是程式可忽略的設定；若仍有 default route，就不是安全邊界。
- 工具白名單與人工 approval 很有用，但屬於操作安全，不足以抵抗惡意或被攻陷的程式。
- 多個 agent 若共用同一 Python process，就共用該 process 的檔案、環境與憑證；要做每實例隔離，必須拆成不同 process/container。

## 文件導覽

- [01-asf-architecture.md](01-asf-architecture.md)：ASF 實際如何組合容器、掛載、網路、憑證與驗證。
- [02-current-boundary.md](02-current-boundary.md)：`freepy` 目前真正能限制與不能限制的範圍。
- [03-recommendation.md](03-recommendation.md)：建議架構、最小版本與逐步落地順序。

## 判斷準則

在文件或 API 聲稱某項限制前，應能回答：

1. 限制由誰執行？受限程式能否修改或繞過？
2. 預設是拒絕，還是漏設定就全部放行？
3. 啟動前是否驗證實際效果，而不只相信產生出的設定？
4. Secret 是否根本沒有進入受限環境？
5. 停止後哪些狀態保留、哪些資源一定清除？

## 調查來源

- [ASF repository](https://github.com/javimox/asf)
- [ASF trust model](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/TRUST.md)
- [ASF security model](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/docs/SECURITY-MODEL.md)
- [ASF network design](https://github.com/javimox/asf/blob/e120daf6a8db573760cdac1b1b2c8bb162fd1186/docs/NETWORK-DESIGN.md)
- [Podman run documentation](https://docs.podman.io/en/stable/markdown/podman-run.1.html)
