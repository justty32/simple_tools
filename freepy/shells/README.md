# shells

幾個一行就能記住的入口，把你丟進對應的 shell。**單純的 proxy，不做別的事** ——
不包裝、不攔輸出、不加參數。

```bash
python -m shells repl        # Python REPL，FreePy 本地 API 已預載
python -m shells pi          # pi coding agent
python -m shells claude      # claude code

./shells/pi.py               # 直接跑也行，兩條路等價
python -m shells pi -c       # 名字後面的參數整包轉給它，這行等於 pi -c
```

Pi 若已設定 `AGENTLOOP_PI_FACTORY`，launcher 會自動加入 FreePy adapter；未設定時仍是原本的
純 pass-through。設定與離線示範見 [Pi launcher](PI.md)。

`python -m shells` 不帶名字會印出目前有哪些入口（名單是掃資料夾算出來的，
新增一個 `.py` 就自動出現在裡面）。

## 它們到底做了什麼

共同部分是兩件事：

1. `PYTHONPATH` 前面補上 `freepy` 和隔壁的 `llmkit`，`import base_tools` /
   `import llms` 才會通（`llms` 和 `tooljson` 已經落地到 `llmkit/`）
2. 把收到的參數原樣交給目標程式，並保留它的退出碼

在 POSIX 上會用 `os.execvp` **把自己換成目標程式**，Ctrl-C、TTY 與退出碼直接交接。
Windows 的 Python `exec` 無法可靠保留含空白的參數與 child 退出碼，因此改用繼承同一個
console 的 child handoff；launcher 會先解析 PATH 上的 `.exe`／`.cmd`，參數與退出碼仍會
原樣傳遞。這讓 `pi`、`claude` 之類的 TUI 和 `repl -c "... ..."` 都能走同一個入口。

`pi` 與 `claude` 另外會把 cwd 固定在 `freepy/`；`repl` 則保留啟動時的 cwd，讓
`base_tools` 的預設 workspace 就是操作者所在的專案。`repl` 會優先用 `freepy/.venv` 裡的
python，不然從沒 activate 的 shell 叫進來會找不到 `openai`。找不到 venv 就退回當前的
python。進入後會印出工具 workspace，並可直接使用 `LLM`、`Engine`、`Params`、
`Controller`、`Handle`、`Assistant`、`assistant`、`toolbox`、`session`，以及 `llms`、`base_tools`、
`agentloop` modules。

`assistant()` 把建 bot 與掛 tools 的重複樣板收在一起；它只接受明確列出的 Python
callable 或既有的 `(schemas, dispatch)` bundle，不會掃描 `PATH`、選 workspace 或建立
sandbox：

```python
# 預設已是啟動 repl 的目錄；要縮到子目錄或改目標時再明確設定
base_tools.set_root(".")
bot, dispatch = assistant("lm-gemma-4-12b", base_tools.tools(), system="先查證再回答。")
c = session(bot, dispatch, "檢查這個專案")
```

在 REPL 裡可省掉解包，直接從配對結果啟動：

```python
c = assistant(
    "lm-gemma-4-12b", base_tools.tools(), system="先查證再回答。"
).session("檢查這個專案")
```

第一個參數也可以是自己建的 `Engine`；`toolbox()` 可單獨合併 callable 與多組 bundle。
`assistant()` 的結果是可解包的 `Assistant(bot, dispatch)`；舊寫法保持不變，`.session()` 則會
使用同一組 bot/dispatch。工具同名或 schema/dispatch 名稱不一致時會立即拒絕，避免 effect
被靜默取代。

`session()` 是 Python library helper，不只限 launcher 裡使用。它建立預設
`auto_finish=False` 的 Handle、Controller 和一條背景 runner：

```python
from shells import session

c = session(bot, dispatch, "先檢查專案")
c.wait()
c.send("整理結論", finish=True)
result = c.join()
```

無參數的 `c.wait()` 會等到 `waiting`／`paused`／`completed`／`error`；要等特定狀態時可用
`c.wait("waiting", timeout=10)`，回傳值與 `Handle.wait_for_state()` 一樣是 bool。

若傳入自己的 `handle=`，其 `auto_finish` 與 callbacks 都保持原樣。

## 加一個新入口

複製 `pi.py` 改個名字就好，三行：

```python
from common import enter

enter("codex")
```

`common.py` 只有 `enter()` 一個函式。每個入口檔都在 20 行以內。
