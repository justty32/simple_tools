# Claude Code host 整合

[← 返回 Claude Code adapter 總覽](README.md)

## Skills／commands 的角色

Claude Code 現在把 custom commands 併入 skills；project skill 放在
`.claude/skills/<name>/SKILL.md`，人可用 `/name` 呼叫，模型也可依 description 自動載入。

建議提供一個薄的 `agentloop` skill，內容只描述：

- 先 `status`，依 state 選擇下一個 MCP action；
- gate 停住時，先核對 step/sequence，再 edit/resume；
- 不把 Claude Code permissions 誤當 agentloop tool approval；
- 不重複 start，不對 completed/error Handle resume；
- 何時應把決定交回人類，尤其是 `end` 與有副作用的 tool-call rewrite。

Skill 是 prompt-based workflow，不是可靠的 policy enforcement，也不能持有 Handle。若某操作
只准人觸發，可用 skill frontmatter 限制 model invocation，並另外用 Claude Code MCP tool
permissions 限制模型是否能直接呼叫 `end`／`edit`。

MCP server 也可以曝光 prompts；Claude Code 會把它們顯示成
`/mcp__servername__promptname` command。第一版可以先用 project skill 取得較好名稱與配套
文件；若未來希望 adapter 在其他專案安裝後自帶 workflow，再同時提供 MCP prompt 或 plugin
skill。

## Hooks 的角色與限制

Claude Code hooks 是 Claude Code 自己的 lifecycle 邊界，例如 `PreToolUse`、`PostToolUse`、
`Stop`、`SessionStart`、`SessionEnd`。它們可執行 command、HTTP、prompt、agent 或已連線的
MCP tool，部分事件可以 block、deny 或修改 Claude Code tool input/output。

它們不等同 agentloop callbacks：

- `PreToolUse` 只看到 Claude Code 將要執行的 tool。agentloop runner 內部模型要求的 Python
  tool calls 是 Handle 資料，不是 Claude Code tool call，hooks 看不到。
- 可以對 `mcp__agentloop__end`／`edit` 等 MCP tools 設 `PreToolUse` hook 或 permission rule，
  控制 Claude 是否可操作 Handle；這是入口權限，不是 agentloop boundary gate。
- `SessionEnd` 沒有 decision control，不能阻止 session 結束；預設 timeout 也很短。它可透過
  MCP tool best-effort 呼叫 `end`，但不能保證長時間 operation 完成與 join。
- MCP-tool hook 要求 server 已連線；`SessionStart` 常早於 MCP server 連線，因此不適合依賴
  它建立 Round。
- async hook 不會阻塞 Claude，完成結果只在仍活著的 session 中送達，也不提供 agentloop
  同步 gate 所需的保證。

所以第一版不需要 hooks 才能運作。等要限制「Claude 不得自行 end」或在 session teardown
做 best-effort cleanup 時再加，並清楚標為 Claude Code policy／cleanup，而非核心控制流。

## Lifecycle 與 teardown

第一版把 stdio MCP server 與 Claude Code session 視為同生命週期，但不要把這理解成持久化
保證：官方文件說 plugin MCP servers 會在 session startup 自動連線，也明確指出 stdio
server crash 後不會自動 reconnect。故設計應假定連線一斷，記憶體 Handle 就不可恢復。

正常關閉次序：

1. MCP server 收到明確 `shutdown`／transport EOF，或 optional `SessionEnd` cleanup action。
2. 若 Handle 尚活著，呼叫 `handle.end(reason="claude-code-disconnect")`。
3. 用有限 timeout 等 `runner.join()`。
4. 若 runner 尚在 model request/tool batch，記錄「safe end 尚未抵達邊界」。
5. server process 最終若被 Claude Code 或 OS 終止，視為不可抗力；不能聲稱 operation 已安全
   收尾。

不要讓 server 為了等一個無限期卡住的 Python tool 而阻止 Claude Code 永遠退出。agentloop
沒有 unsafe cancellation，因此「立即關掉 process」和「保證已開始 operation 完整提交」
無法同時成立。

若需求變成 Claude `/resume`、Claude process restart 後仍保留活著的 Round，stdio child
就不夠。那時應升級成獨立的 localhost HTTP MCP service 或其他 service IPC，加入 Round
registry、authentication、租約、重連與持久化；不是把 Handle 序列化給新 process。
