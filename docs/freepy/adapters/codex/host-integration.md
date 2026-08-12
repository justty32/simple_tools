# Codex host 整合

[← 返回 Codex adapter 總覽](README.md)

## 方案比較

| 方案 | 優點 | 問題 | 結論 |
|---|---|---|---|
| **Codex TUI + STDIO MCP + skill** | 使用現成 UI、tools 與 approvals；Python process 持有 Handle；協議標準；可逐步包成 plugin | 要實作 snapshot、event queue 與 lifecycle；無任意 Python object 存取 | **推薦第一版** |
| 只寫 skill | 最少內容；能教 Codex 執行既有 scripts | Skill 是 instructions/resources，不是活的 process bridge；無法持有 Handle 或提供可靠同步工具 | 只作 MCP 的操作說明 |
| Plugin（skill + MCP） | 一包安裝與散佈；可以同時帶 workflow 與 server wiring | 不會消除 process 邊界；開發早期增加 manifest、安裝與版本管理 | 驗證後封裝 |
| Python Codex SDK + agentloop | 同一 Python host 可同時管理 Codex app-server client 與 agentloop Handle；適合程式化 orchestration | 沒有現成 Codex TUI；host 要自己提供 UI、approval handling 與兩套 lifecycle | 自訂產品／Agent Machine 再用 |
| 直接使用 Codex app-server | 最完整 thread/turn/item events、approvals、streaming；適合深度 client | 協議面大；自建 client；WebSocket transport 官方仍標示 experimental/unsupported | 不是第一個互動入口 |
| `codex exec --json` subprocess | 腳本與 CI 很簡單；JSONL events、structured output、session resume | 每次是 headless job；沒有自然的長期 Handle controller；若仍靠 MCP，核心方案其實還是 MCP | 批次任務，不作主入口 |
| Codex shell 操作 Python REPL／tmux | 幾乎不用 adapter | parsing 脆弱、無 schema/correlation、容易 orphan、無可靠 callback gate | 只供人工試驗 |
| 把 Codex CLI本身開成 MCP server給 agentloop | 官方支援 Codex 作為其他 orchestrator 的 specialist | 控制方向相反：是 agentloop/Agents SDK 呼叫 Codex，不是 Codex 操作 agentloop | 可供未來 Agent Machine |

## Plugin 與 skill 應該做到哪裡

Pi extension 可以直接註冊 TUI command、custom tool 與 UI；Codex 的對應物不是單一 extension
API，而是不同責任的組合：

- **MCP server**：live state、控制 actions、schema、authorization 與 process boundary。
- **Skill**：可重複的操作順序，例如「先 status，再 pause+wait，帶 expected sequence edit，
  最後 resume」；也記錄不得混淆 Codex interrupt 與 agentloop end。
- **Plugin**：把 skill、MCP server manifest 與必要 metadata 包成可安裝單位。

所以第一版先在 repository 放 project MCP config、server script 與 project skill，方便直接改與
測試。等 tool schema、factory contract 與 lifecycle 穩定後，再建立 `.codex-plugin/plugin.json`
與 `.mcp.json` 封裝。Plugin 是發行階段，不應承擔 agentloop runtime 語意。

Skill 至少應要求 Codex：

1. 控制前先取得最新 status。
2. 修改 tool calls/results 前先 pause 並確認真的進入 paused。
3. edit 一律帶 expected state/step/sequence；衝突就重讀，不盲目重試舊 patch。
4. 修改後只有明確 resume 才繼續。
5. 任務不再繼續時呼叫 end，並確認 completed/error。
6. 不同時讓 Codex coding tools 與 agentloop tools 寫同一批檔案。
7. 限制 status→wait→status 的循環次數、等待時間與 agentloop Limits。

## 權限與安全

MCP tool approval 是 Codex host 的 policy，不是 agentloop 的安全邊界。尤其 `resume` 可能放行
尚未執行的 Python tool calls，應視為可能產生副作用的控制 action。

初版建議：

- `status`、短 timeout 的 `wait` 可自動執行；
- `start`、`edit`、`resume`、`end` 預設要求核准；
- `pause` 可自動或要求核准，視使用者是否接受干擾 Round；
- MCP server 的 `enabled_tools` 只開第一版所需工具；
- factory、可用 dispatch tools 與檔案 roots 由 Python 應用層 allowlist；
- Codex 與 agentloop 使用同一工作樹時，必須指定單一 writer 或使用 worktree 隔離。

Codex sandbox 約束 Codex 自己的 command/file operations；STDIO MCP server 及其 Python tools
的實際權限還取決於 host、MCP 啟動方式與 OS 環境。不要因 Codex 顯示 workspace-write 就假設
agentloop dispatch 自動受到相同限制。
