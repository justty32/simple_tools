# Codex adapter 的交付與決策

[← 返回 Codex adapter 總覽](README.md)

## 預計文件、scripts 與 examples

後續若實作，可採：

```text
repl/
├── docs/
│   └── codex.md
├── scripts/
│   └── codex_mcp.py             # MCP server、registry、snapshots、events、shutdown
├── skills/
│   └── agentloop-controller/
│       └── SKILL.md             # Codex 的可靠操作流程
└── examples/
    ├── codex_minimal_factory.py # 建立 bot/dispatch 的具名 factory
    ├── codex_project_config.toml
    └── codex_gate_tools.md      # after_step/after_tools gate 範例
```

第一個驗收場景：啟動 `auto_finish=False` Round，after-step gate 暫停；Codex 讀取並改寫一個
tool call，resume；after-tools gate 再檢查結果；最後 end/join。它能一次驗證 MCP schema、
過時修改防護、兩個 callback boundary 與正常 teardown。

## 待決問題

1. Python factory 採固定 registry、`module:function` allowlist，還是由 server entrypoint 直接
   import 應用程式？固定 registry 最容易限制。
2. `agentloop_edit` 採固定 operations 或 JSON Patch 子集？固定 operations 適合第一版；成熟後
   可加入受限 JSON Patch。
3. 第一版是否允許 Codex 模型自己 `resume`／`end`，或這兩項每次都要人核准？這是 host
   policy，不應寫進 agentloop 核心。
4. Gate 預設是 only `after_step`，還是兩個 boundary 都 pause？展示版建議兩者都 pause。
5. MCP shutdown timeout 後是讓 daemon runner 隨 process 結束，還是留下警告並等待人工處理？
   agentloop 無法同時保證立即退出與 operation 完整。
6. 何時升級 Streamable HTTP？應以跨 session persistence、多 client 或 remote host 的實際需求
   為門檻。
7. Codex 與 agentloop 是否允許共用寫入工具？如果允許，誰負責 worktree lock 與衝突復原？

## 官方資料

以下均為 OpenAI 官方資料；本文先以當日重新取得的 Codex manual 定位並精讀相關章節：

- [Codex manual](https://developers.openai.com/codex/codex-manual.md)：官方 Codex 綜合手冊；
  本文核對 Noninteractive and Programmatic Interfaces、MCP、Skills 與 Plugins 章節。
- [Model Context Protocol](https://learn.chatgpt.com/docs/extend/mcp.md)：Codex 支援的 STDIO／
  Streamable HTTP transports、project config、tool allowlist、timeout 與 approval mode。
- [Codex App Server](https://learn.chatgpt.com/docs/app-server.md)：JSON-RPC transports、
  thread/turn/item lifecycle、stream events、approvals、steer 與 interrupt。
- [Codex SDK](https://learn.chatgpt.com/docs/codex-sdk.md)：TypeScript 與 Python SDK；Python SDK
  控制 local app-server，適合自訂 host 與程式化 orchestration。
- [Non-interactive mode](https://learn.chatgpt.com/docs/non-interactive-mode.md)：
  `codex exec`、JSONL events、structured output、sandbox 與 session resume。
- [Skills](https://developers.openai.com/plugins/concepts/skills)：skill 是 workflow instructions 與
  resources，MCP server 提供 live data 與 controlled actions。
- [Plugins](https://learn.chatgpt.com/docs/plugins)：Codex plugin 可組合 skills 與 MCP servers，
  Codex CLI 提供 plugin browser。
- [Build plugins](https://learn.chatgpt.com/docs/build-plugins)：`.codex-plugin/plugin.json`、
  bundled skill 與 MCP manifest 的封裝方式。
