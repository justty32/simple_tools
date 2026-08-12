# 下一版 Python 互動 API

> **狀態：已決定，尚未實作。** 這份記錄 2026-08-12 實際操作 Python REPL 後確定的
> 介面方向；目前可執行的 API 仍以 [`python-repl.md`](python-repl.md) 為準。

## 操作者的概念模型

Python API 應直接對應使用者理解 bot 的方式：

```text
LLM
└── 思考引擎：endpoint、model、生成參數

Bot
├── LLM
├── system prompt：人格與長期行為準則
├── message history：對話記憶
└── tools：可供模型選擇、也能真正執行的能力

Bot.start(instruction)
└── 啟動一個 Round，立即回傳 Controller
```

一個 Round 從 bot 接收一筆指令開始，可以包含多個 Step、工具呼叫與操作者追加指令，
直到操作者或 policy 確定結束。Round 與 Step 的既有定義不變，見
[`ROUNDS.md`](../../../freepy/agentloop/ROUNDS.md)。

## 目標介面

```python
bot = Bot(
    LLM(model="deepseek-chat"),
    system="先讀檔查證，再用繁體中文簡短回答。",
    tools=base_tools.read_file,
)

c = bot.start("請讀取 README.md，說明主要元件。")
c.wait()
print(c.handle.message)
```

需要時仍可寫成一個 expression：

```python
c = Bot(
    LLM(model="deepseek-chat"),
    system="請簡短回答。",
    tools=base_tools.read_file,
).start("讀取 README.md")
```

## 命名決定

- `LLM` 只代表思考引擎，持有 endpoint、model 與生成參數；人格、history 和 tools 屬於
  `Bot`。
- `Bot` 建構完成時就是可行動的完整個體。工具不是事後裝上的另一種物件，因此不引入
  `equip()` 或 `EquippedBot`。
- 捨棄 `Assistant`／`assistant()`，避免與 message history 裡的 `assistant` role 在說明時
  混淆。
- 捨棄 `.session()`。目前建立的是一個 Round，不是可容納多個 Round 的長期 session。
- 使用 `.start(instruction)`：它表達啟動背景 Round，不暗示同步跑到結束，也比
  `.start_session()` 簡潔。

message history 的 role 名稱仍遵循模型 API：`user`、`assistant`、`tool`。這裡的
`assistant` 只表示「該 message 由模型產生」，不再同時是 FreePy 物件型別名稱。

## tools 契約

傳給 `Bot(..., tools=...)` 的每項能力必須同時包含：

1. 給 LLM 選擇的工具名稱、說明與參數 schema；
2. 模型選擇後真正呼叫的 Python callable／dispatch。

對外 API 可接受單一 callable、callable 集合或既有 `(schemas, dispatch)` bundle，但進入
`Bot` 時必須完成名稱配對與衝突檢查。不能出現「模型看得到工具定義，runtime 卻沒有對應
函數」的半套能力。workspace、OS 權限、effect policy 與核准流程仍由呼叫者明確決定；
擁有 tool 不等於建立 sandbox。

## start 與 wait

`bot.start(instruction)` 建立新的 Handle、Controller 與背景 runner，啟動一個 Round 後立即
回傳 Controller。它不等待模型完成，因此操作者能在同一個 REPL 觀察和控制進行中的 Round。

`c.wait()` 是操作者執行緒上的阻塞操作；背景 runner 仍繼續執行。無參數時等到
`waiting`、`paused`、`completed` 或 `error` 任一可介入／終止狀態，抵達後回傳 `True`。
若呼叫時已在其中一個狀態就立即返回；指定 timeout 而未抵達則回傳 `False`，但不取消背景
模型請求或工具執行。

## 與目前 API 的概念對照

這不是承諾機械式 rename；實作前仍要盤點 history ownership 與相容策略。目標概念大致是：

| 目前介面 | 下一版概念 |
|---|---|
| `Engine(...)` | `LLM(...)` |
| `LLM(engine, system=...)` | `Bot(llm, system=..., tools=...)` 的一部分 |
| `assistant(model, tools, system=...)` | `Bot(LLM(...), system=..., tools=...)` |
| `setup.session(prompt)` | `bot.start(instruction)` |
| `c.wait()` | 保留 |

實作時要保證：同一個 Bot 的 history 可跨先後 Round 延續，但同一個 mutable Bot 不可同時啟動
兩個 Round；工具 schema 與 dispatch 在 `start()` 前已驗證完整。
