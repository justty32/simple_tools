"""shell.py — 跑一行 POSIX shell 指令。

**只服務 POSIX。**不是懶得做相容，是因為這套東西的整個前提就是「linux 該有的都有」——
模型腦子裡的 shell 是 POSIX，它寫出來的 `;`、`&&`、`test -f`、單引號都是 POSIX 的。
Windows 上 `shell=True` 會走 cmd.exe，而 cmd **不認得 `;` 是分隔符**：

    test -f a.txt && echo 'A' ; test -d b && echo 'B'

    cmd.exe      "'A' \\n"          <- 分號後面整段被當成前面 echo 的參數吞掉
    /bin/sh      "A\\nB\\n"

回傳 exit 0，沒有任何錯誤。模型問了兩個問題只拿到一個答案，而且它不知道 ——
然後它會拿這個殘缺的認知去規劃。安靜的錯比吵的錯危險得多，所以非 POSIX 直接擋下來，
不去嘗試翻譯語法（翻不完的，而且翻錯一樣是安靜的）。要在 Windows 上開發就進 WSL。

這是四個工具裡唯一真的危險的：模型講什麼就執行什麼，root 也擋不住 `curl | sh`。
黑名單擋不住有心的指令組合（換個寫法就繞過去了），所以這裡不假裝有黑名單，
只給一個守門員 hook，要人看過才放行的話自己接上去：

    base_tools.set_approver(lambda cmd: input(f"跑 {cmd} ？[y/N] ") == "y")

預設是 None，全部放行 —— 你自己一個人跑著玩沒差，接到會自己動的 agent 上就該接。
"""

import os
import subprocess

from .paths import clip, get_root

_approver = None


def set_approver(fn):
    """設一個 fn(command) -> bool 的守門員，回 False 就不執行。傳 None 取消。"""
    global _approver
    _approver = fn


def _decode(raw):
    if raw is None:
        return ""
    if isinstance(raw, bytes):
        return raw.decode("utf-8", errors="replace")
    return raw


def run_shell(command: str, timeout: int = 60) -> str:
    """在工作根目錄底下執行一行 shell 指令，回傳它的輸出和結束碼。

    Args:
        command: 要執行的指令，就是你會在終端機打的那一行
        timeout: 最多等幾秒，逾時會殺掉並回傳已經印出來的部分
    """
    if os.name != "posix":
        # 擋在這裡而不是讓 cmd.exe 去猜：猜錯是安靜的，模型會拿殘缺的輸出繼續規劃
        return (
            f"Error: run_shell requires a POSIX shell, but this is os.name={os.name!r}. "
            "Run under WSL or Linux."
        )
    if not isinstance(command, str) or not command.strip():
        return "Error: command must be a non-empty string"
    try:
        seconds = max(1, int(timeout))
    except (TypeError, ValueError):
        return f"Error: timeout must be an integer, got {timeout!r}"

    if _approver is not None:
        try:
            allowed = _approver(command)
        except Exception as e:
            return f"Error: approval check failed: {e}"
        if not allowed:
            return "Error: the user declined to run this command"

    try:
        done = subprocess.run(
            command, shell=True, cwd=str(get_root()), timeout=seconds,
            capture_output=True, text=True, errors="replace",
        )
    except subprocess.TimeoutExpired as e:
        # 逾時的部分輸出仍然有用，模型至少知道它卡在哪
        partial = (_decode(e.stdout) + _decode(e.stderr)).strip()
        head = f"Error: command timed out after {seconds}s"
        return clip(f"{head}\n{partial}" if partial else head)
    except Exception as e:
        return f"Error: cannot run command: {e}"

    out = (done.stdout or "") + (done.stderr or "")
    out = out.strip()
    if done.returncode == 0:
        return clip(out) if out else "(no output, exit 0)"
    return clip(f"exit {done.returncode}\n{out}" if out else f"exit {done.returncode} (no output)")
