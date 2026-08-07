#!/usr/bin/env python3
"""plan_shell.py — 把一句人話翻譯成一串 shell 指令。

第一個純工具原型。重點不是模型聰不聰明，是**介面**：這支程式對外長得跟
`grep` 沒兩樣 —— 吃文字、吐文字、有退出碼 —— 只是它內部剛好塞了一顆 LLM。

    uv run python prototypes/plan_shell.py "把 a.txt 移到 backup/"
    echo "把 a.txt 移到 backup/" | uv run python prototypes/plan_shell.py
    uv run python prototypes/plan_shell.py --root /tmp/ws --model deepseek-chat "..."

介面：

    stdin / argv   人話
    stdout         翻譯結果，只有這個 —— 乾淨到可以直接 `> plan.sh`
    stderr         過程（模型探查了什麼），給人看的，不是產物
    exit 0         翻出計劃了
    exit 2         資訊不足，stdout 是它要問你的問題
    exit 1         壞掉了，stderr 有原因

**它只翻譯，不執行。**產出的 shell 要不要跑是你的事 —— 這是刻意的分界：
翻譯是不確定的（模型會錯），執行是不可逆的（rm 收不回來），兩件事不該綁在一起。

模型有三個出口，就是 note 2026-08-06 第 24 行那組「通用的」工具：

    inspect    先看現場，結果餵回去再想 —— 翻譯器的內部運作，不進產物
    plan       翻得出來了，交出 shell
    ask_user   翻不出來，需要人補資訊
"""

import argparse
import sys
from pathlib import Path

# 這支是直接跑的腳本，不是被 import 的 module，所以自己把 freepy/ 接上去
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import base_tools                                        # noqa: E402
from llms import LLM, to_schemas                         # noqa: E402

MODEL = "ollama-qwen3-32b"
ROUNDS = 6                     # 探查幾輪還交不出計劃就放棄，免得模型原地打轉

SYSTEM = """你是一個翻譯器：把使用者的檔案操作意圖，翻譯成一串 shell 指令。

你不執行任何事，只產出計劃。規矩：

1. 不知道現場長怎樣就用 inspect 去看，不要用猜的。檔案在不在、是檔案還是資料夾、
   目標資料夾存不存在 —— 這些都要看過才算數。
2. 看清楚了就用 plan 交出 shell。
3. 使用者沒講清楚（哪個檔、放到哪裡、同名了要不要蓋），用 ask_user 問，
   不要替他決定。

plan 的 script 照這個順序寫：先確認來源存在，再動手，最後確認結果。
每一步前面用 # 說明為什麼。只用 POSIX shell，不要用會停下來等人的指令。

**inspect 看到的結果不能拿來取代 script 裡的檢查。**你規劃完之後，這份 script
可能過幾秒、也可能過三天才被執行，中間檔案會變。inspect 是用來決定「要做什麼」的，
「現在能不能做」得由 script 自己在執行當下重新確認：條件不成立就 exit 1 停下來，
不要硬做。所以檢查要寫成 if / test 擋在前面，不能只寫一行註解說你確認過了。
"""


def inspect(command: str) -> str:
    """跑一個唯讀的 shell 指令看看現場，例如 ls -la、test -f x && echo yes、file x。

    Args:
        command: 要跑的唯讀指令。不要用它改任何東西，改的事情寫進 plan 裡。
    """
    return base_tools.run_shell(command)


def plan(goal: str, script: str) -> str:
    """交出翻譯結果：一段可以直接執行的 shell。確定要做什麼之後才叫這個。

    Args:
        goal: 一句話說明這段 shell 要達成什麼
        script: shell 本體，一行一步，每步前面用 # 說明為什麼
    """
    # 沒有實作是故意的：plan 和 ask_user 不是「函式」，是這支工具的兩個出口。
    # 它們存在的意義只是讓模型有辦法**結構化地**表達「我翻完了」跟「我翻不了」，
    # 而不是讓我們去猜它那段散文裡哪幾行才是指令。


def ask_user(question: str) -> str:
    """資訊不足時問使用者。問了就停，不要一邊問一邊猜。

    Args:
        question: 要問使用者的問題，一次問完，講清楚你卡在哪
    """


TOOLS = (inspect, plan, ask_user)


def note(text):
    """過程一律走 stderr —— stdout 是產物，不能混。"""
    print(text, file=sys.stderr, flush=True)


def handle(call):
    """處理一個 tool call，回傳 (exit_code, output) 表示該收工，或 None 表示繼續探查。"""
    name, args = call["name"], call["args"]

    if name == "plan":
        note(f"[plan] {args.get('goal', '(沒說目標)')}")
        return 0, args.get("script", "")
    if name == "ask_user":
        note("[ask_user] 模型要問你話")
        return 2, args.get("question", "(沒問出問題)")
    return None


def translate(bot, schemas, prompt):
    """讓模型探查到它交得出計劃為止。回傳 (exit_code, 要印到 stdout 的東西)。"""
    result, err = bot.ask(prompt, tools=schemas)

    for _ in range(ROUNDS):
        if err:
            return 1, f"llm error: {err}"

        if not isinstance(result, list):
            # 模型直接講話了 —— 小模型很常這樣，推它一把再給一次機會
            note(f"[note] 模型沒用工具，回了一段話：{result}")
            result, err = bot.ask("請用工具回答，不要直接說話。", tools=schemas)
            continue

        results = {}
        for call in result:
            done = handle(call)
            if done:
                return done

            command = call["args"].get("command")
            if call["name"] != "inspect" or not command:
                out = f"Error: unknown tool call {call['name']}({call['args']})"
            else:
                note(f"[inspect] {command}")
                out = inspect(command)
                note("".join(f"    {line}\n" for line in out.splitlines()))
            results[call["id"]] = out

        result, err = bot.ask(tool_results=results, tools=schemas)

    return 1, f"探查了 {ROUNDS} 輪還沒交出計劃，放棄"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("prompt", nargs="*", help="人話。不給就從 stdin 讀")
    ap.add_argument("--model", default=MODEL, help=f"翻譯用的模型（預設 {MODEL}）")
    ap.add_argument("--root", default=".", help="現場在哪，inspect 只能在這底下看")
    args = ap.parse_args()

    prompt = " ".join(args.prompt) or sys.stdin.read().strip()
    if not prompt:
        note("沒有輸入。給我一句話，或從 stdin 餵進來。")
        return 1

    base_tools.set_root(args.root)
    note(f"[model] {args.model}  [root] {base_tools.get_root()}")

    bot = LLM(model=args.model, system=SYSTEM, timeout=300)
    code, out = translate(bot, to_schemas(*TOOLS), prompt)
    print(out)
    return code


if __name__ == "__main__":
    sys.exit(main())
