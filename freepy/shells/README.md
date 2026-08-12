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

`python -m shells` 不帶名字會印出目前有哪些入口（名單是掃資料夾算出來的，
新增一個 `.py` 就自動出現在裡面）。

## 它們到底做了什麼

三件事，就這樣：

1. `os.chdir` 到 `freepy/`，所以不管你從哪裡叫，agent 看到的工作目錄都一樣
2. `PYTHONPATH` 前面補上 `freepy` 和隔壁的 `llmkit`，`import base_tools` /
   `import llms` 才會通（`llms` 和 `tooljson` 已經落地到 `llmkit/`）
3. `os.execvp` **把自己換成目標程式**，不是開子 process

第三點是重點：換掉自己之後 Ctrl-C、TTY、退出碼全部直通，中間沒有人插手。
開子 process 的話這三樣都要自己轉一遍，而且轉不乾淨 —— TUI 程式（pi、claude 都是）
對這個特別敏感。

`repl` 會優先用 `freepy/.venv` 裡的 python，不然從沒 activate 的 shell 叫進來會找不到
`openai`。找不到 venv 就退回當前的 python。進入後可直接使用 `LLM`、`Engine`、`Params`、
`Controller`、`Handle`、`session`，以及 `llms`、`base_tools`、`agentloop` modules。

`session()` 是 Python library helper，不只限 launcher 裡使用。它建立預設
`auto_finish=False` 的 Handle、Controller 和一條背景 runner：

```python
from shells import session

c = session(bot, dispatch, "先檢查專案")
c.handle.wait_for_state("waiting")
c.send("整理結論", finish=True)
result = c.join()
```

若傳入自己的 `handle=`，其 `auto_finish` 與 callbacks 都保持原樣。

## 加一個新入口

複製 `pi.py` 改個名字就好，三行：

```python
from common import enter

enter("codex")
```

`common.py` 只有 `enter()` 一個函式。每個入口檔都在 20 行以內。
