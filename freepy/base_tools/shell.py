"""shell.py — 跑一行 shell 指令。

這是四個工具裡唯一真的危險的：模型講什麼就執行什麼，root 也擋不住 `curl | sh`。
黑名單擋不住有心的指令組合（換個寫法就繞過去了），所以這裡不假裝有黑名單，
只給一個守門員 hook，要人看過才放行的話自己接上去：

    base_tools.set_approver(lambda cmd: input(f"跑 {cmd} ？[y/N] ") == "y")

預設是 None，全部放行 —— 你自己一個人跑著玩沒差，接到會自己動的 agent 上就該接。
"""

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
