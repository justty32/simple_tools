# base_tools

給 LLM 用的四個基本工具：**讀檔、寫檔、改檔、跑指令** —— 一個 coding agent 最少需要的那組。
配 [llms](../llms/README.md) 用，schema 直接從函式本體生出來。

```python
import base_tools
from llms import LLM

base_tools.set_root("/tmp/workspace")     # 模型只能在這底下動手腳
schemas, dispatch = base_tools.tools()

bot = LLM(model="deepseek-chat", system="你是一個會用工具處理檔案的助手。")
result, err = bot.ask("把 a/b.txt 裡的 two 改成 TWO", tools=schemas)
while isinstance(result, list):           # 模型要叫工具就照做，結果送回去，直到它給文字答案
    results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in result}
    result, err = bot.ask(tool_results=results, tools=schemas)
print(result)
```

## 四個工具

| 函式 | 參數 | 回傳 |
|---|---|---|
| `read_file` | `path, offset=1, limit=2000` | 帶行號的內容，跟 `cat -n` 一樣 |
| `write_file` | `path, content` | `Created/Overwrote 路徑 (N lines, M bytes)` |
| `edit_file` | `path, old, new, replace_all=False` | `Replaced N occurrence(s) in 路徑` |
| `run_shell` | `command, timeout=60` | 指令輸出（stdout + stderr），非 0 時前面加 `exit N` |

沒有列目錄的工具，因為 `run_shell("ls -la")` 就夠了。

**回傳的永遠是一個字串，錯誤也是字串**，不丟例外、也不是 `llms` 那種 `(result, err)`。
理由：回傳值會直接變成送回模型的 tool message，錯誤訊息本身就是要給模型讀的東西 ——
它看到 `Error: file not found: x.txt` 會自己去找正確路徑，看到一個 tuple 只會困惑。
訊息用英文，因為模型看過的工具輸出九成長那樣；工具說明（description）則是中文，
那是從 docstring 第一行抓的。

幾個刻意的設計：

- `read_file` **一定帶行號**，模型引用位置時有東西可指。二進位檔直接擋掉，空檔案會明講
  「is empty」而不是回一個空字串（回空字串模型會以為工具壞了）。太大就分次讀，
  結尾會告訴它下一次的 `offset`。
- `edit_file` 只做**一字不差的字串取代**，不收 diff、不收正則。模型產生的 unified diff
  行號常常對不上，但「把這段換成那段」它幾乎不會弄錯。`old` 在檔案裡出現不只一次時
  會**拒絕動作**並回報出現幾次 —— 不然模型以為改了一處其實動到三處，而且它不會知道。
- `run_shell` 的 stdout 和 stderr **併在一起**，因為模型要的是「發生什麼事」，
  分兩欄只是多一層它要自己拼回去的結構。逾時會殺掉，已經印出來的部分照樣回傳。

## 關在 root 裡

所有路徑都先接到工作根目錄底下再 `resolve()`，解析完不在 root 裡就拒絕 ——
`../../etc/passwd`、絕對路徑、symlink 想繞出去，都在同一個地方擋掉。

```python
base_tools.set_root("/tmp/workspace")   # 預設是啟動時的 cwd
base_tools.get_root()
```

`run_shell` 也在 root 底下跑（`cwd`），但**它擋不住想跑出去的指令** —— `cd /`、
`curl | sh`、絕對路徑的 `rm`，root 一個都攔不了。這是四個工具裡唯一真的危險的一個。

這裡不放黑名單，因為黑名單擋不住有心的指令組合（換個寫法就繞過去了），
反而讓人以為安全。要人看過才放行的話自己接守門員：

```python
base_tools.set_approver(lambda cmd: input(f"跑 {cmd} ？[y/N] ") == "y")
base_tools.set_approver(None)           # 取消，全部放行（預設）
```

回 `False` 時模型收到的是 `Error: the user declined to run this command`，
它看得懂，會改用別的做法或問你。

真的要放生一個會自己動的 agent，就別只靠這個：跑在容器或 VM 裡才是對的答案。

## 輸出上限

單次回傳截在 30000 字元、單行截在 2000 字元（`paths.py` 的 `MAX_OUTPUT` / `MAX_LINE`），
截掉時會在結尾註明省略了多少 —— 讓模型知道它看到的不是全部。
不設上限的話一個 `cat` 大檔就能把 context 灌爆。

## 驗證

```bash
uv run python -m base_tools                  # 21 關離線檢查，跑在臨時資料夾裡
uv run python -m base_tools deepseek-chat    # 多一關：真的讓模型用這些工具改檔案
```

離線那部分不碰網路也不碰你的檔案，改完 code 先跑這個。

## 檔案分工

| 檔案 | 負責 |
|---|---|
| `paths.py` | 工作根目錄、路徑檢查、輸出截斷 |
| `files.py` | `read_file` / `write_file` |
| `edits.py` | `edit_file` |
| `shell.py` | `run_shell` 和守門員 hook |
| `__main__.py` | 煙霧測試 |

一個檔一件事，程式和文件都在 150 行以內。
