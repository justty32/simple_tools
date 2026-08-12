# introspection_tools 規劃

**這份是實作規格，尚未寫程式。** 這層讓模型查看自己當下的狀態與有效能力；所有操作唯讀。

## 為什麼獨立一層

資訊分散在 `Handle`、`Limits`、dispatch、runtime policy 與 team grants。它們不是 communication、memory 或 organization 的責任，也不能塞進固定 system prompt：當前時間與剩餘預算一直變，會破壞 prompt prefix cache。

## 工具

第一版只提供一個聚合工具：

```python
self_status(detail="summary")  # summary | tools | resources | organization
```

摘要包含：

- 當前 `round_id`、第幾步、phase、已執行工具與 token／時間。
- 回合、步、工具總數、單一工具的已用／剩餘預算。
- workspace 與「這是工具 API root 還是 OS sandbox mount」的精確說明。
- effective tools、engines、mount mode、network policy、CPU/RAM/PID/GPU 上限。
- canonical agent path、team subtree、主管、直屬下屬、當前任務與被授予權限；沒有 team 時明說。

不要只說「你被關在 workspace」：若只有 `base_tools` path check 而沒有 container，必須說 shell/exec 仍有宿主權限，避免製造假的安全感。

## 綁定方式

```python
introspection_tools.bind(
    handle=h,
    limits=limits,
    dispatch=dispatch,
    runtime_view=read_only_policy,
    organization_view=read_only_member,
)
```

模型不能提供 handle、identity 或 policy path。回傳的是 bind 當下物件的 snapshot；不同 agent 不能共用 module global，正式實作應產生 closure/object dispatch。

## 回合與步

`self_status` 本身是一個工具呼叫，不豁免工具預算。報告要明寫統計截點，例如「本次 self_status 已計入 calls，但結果尚未計入 history」。不要為了自我報告在 `Limits` 裡開第一個免費工具例外。

當前時間由呼叫當下取得。工具內部的互動不額外展開成 agentloop phase；外層只看得到
正在執行哪個工具與已經過多久。

## 實作順序與測試

1. 定義不含 mutation method 的 `StatusView` protocol。
2. 從 Handle/Limits 建 loop snapshot。
3. 接 dispatch 與 runtime effective policy，而不是原始 request policy。
4. 接 optional team member/task view。
5. 產出人讀得懂但有固定 section 的字串。

必測：剩餘數不為負、未知上限顯示 unlimited、effective policy 與 request 區分、無 sandbox 時不誤稱受限、不同 agent 不串狀態、secret/tool-input 不外洩。
