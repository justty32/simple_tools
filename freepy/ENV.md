# 這台機器的環境

Manjaro + uv + VS Code 的設定，和跨到 Windows 會踩到的東西。**跟 llmkit 的用法無關**，
那些在 [`llmkit/`](llmkit/README.md)；這份只記「為什麼環境要這樣弄」。

## python 和 venv

**系統 python 裝不了東西是兩個原因疊在一起**，別搞混：PEP 668 擋 pip 寫進 pacman
管的 `site-packages`（這是保護，不要用 `--break-system-packages` 繞）；而且系統
python 是 **3.14**，太新，很多套件還沒有 wheel。

所以：

- **python 用 uv 自己管的 3.13**（`~/.local/share/uv/python/`）。除了 wheel 比較齊，
  還躲掉 Arch 的經典坑 —— `pacman -Syu` 把系統 python 升版時，建在它上面的 venv
  會整個死掉。
- **litellm 不裝進任何 venv**，用 `uv run --with 'litellm[proxy]'` 臨時拉起來。
- **整個 freepy 只有一個 venv**（`freepy/.venv`），`llmkit/` 是它底下的一層，
  不另外開。從 `freepy/llmkit` 跑 `uv run` 時 uv 會往上找到 `freepy/pyproject.toml`。
  那份 pyproject 有 `[tool.uv] package = false` —— 沒有這行 uv 會要你補 build backend。

## VS Code / 跨到 Windows

- **`python.analysis.extraPaths` 要有 `freepy/llmkit` 和 `freepy` 兩層**
  （`llms` / `tooljson` 在前者，`base_tools` 在後者）。開整個 repo 當 workspace 時
  Pylance 從 root 找，`from llms import LLM` 會被畫紅線。`launch.json` 的 `cwd`
  設成 `freepy/llmkit` 是同一個原因。
- **直譯器路徑沒辦法分平台寫**（linux 是 `.venv/bin/python`，Windows 是
  `.venv\Scripts\python.exe`），Windows 上第一次要自己選一次。選擇存在本機不進版控。
- **`.gitattributes` 是必要的，不是潔癖**。沒有它，Windows checkout 會把 `.sh` 轉成
  CRLF，回到 Linux 執行就是 `bad interpreter: /usr/bin/env bash^M`。
- PowerShell 預設擋 `.ps1`，`start_litellm.ps1` 跑不動是 ExecutionPolicy 的問題。
