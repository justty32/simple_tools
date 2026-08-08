"""try.py — 隨手試的地方。現在放的是：讓模型拿 base_tools 做事，順便看它在想什麼。

    uv run python try.py                    # 預設 deepseek-reasoner
    uv run python try.py lm-gemma-4-e4b     # 換模型

做事的地方是臨時資料夾，跑完就沒了，不會動到你的檔案。
"""

import sys
import tempfile

import base_tools
from llms import LLM, Engine

MODEL = sys.argv[1] if len(sys.argv) > 1 else "deepseek-reasoner"
TASK = "notes.txt 裡每樣水果各幾個？把總數寫進 total.txt，然後告訴我加起來是多少。"


def show(reply):
    """邊收邊印：[think] 是思考，[answer] 是答案，兩種都即時。"""
    mode = None
    for kind, ch in reply.parts():
        if kind != mode:                       # 換頻道才印一次標籤，不要每個字都印
            print(f"\n[{kind}] ", end="", flush=True)
            mode = kind
        print(ch, end="", flush=True)
    print()


def agent(bot, dispatch):
    """一問一答，模型要叫工具就照做，把結果送回去，直到它給出文字答案。"""
    reply = bot.ask(TASK, stream=True)
    for _ in range(8):                         # 給幾回合就好，別讓卡住的模型無限打轉
        if not reply:
            return reply.err
        show(reply)                            # 這一步會把串流跑完，之後才讀得到 calls
        if not reply.calls:
            return reply.err
        for c in reply.calls:
            print(f"  -> {c['name']}({c['args']})")
        results = {c["id"]: dispatch[c["name"]](**c["args"]) for c in reply.calls}
        reply = bot.ask(tool_results=results, stream=True)
    return "回合用完了，模型還在叫工具"


with tempfile.TemporaryDirectory() as tmp:
    base_tools.set_root(tmp)                   # 模型只能在這底下動手腳
    base_tools.write_file("notes.txt", "蘋果 3\n香蕉 5\n橘子 2\n")

    schemas, dispatch = base_tools.tools()     # schema + name -> function 對照表
    bot = LLM(engine=Engine(model=MODEL), tools=schemas,
              system="用繁體中文回答。要看檔案或改檔案就用工具，不要用猜的。")

    print(f"model: {MODEL}  工作目錄: {base_tools.get_root()}\n")
    err = agent(bot, dispatch)
    print("\nerr:", err)
    print("\n模型寫出來的 total.txt:")
    print(base_tools.read_file("total.txt"))

    # 非串流的 Reply 建好就填滿了，思考跟串流時是同一個欄位，只是不用等
    reply = bot.ask("剛剛那個總數是奇數還偶數？只回一個詞。")
    print("\n再問一句:", reply.text, "err:", reply.err)
    thought = reply.reasoning                  # 混合式思考的模型可能懶得想，那就是 None
    print("這一句的思考:", (thought[:100] + "…") if thought else None)
    print("這一句花了:", reply.usage, "停在:", reply.finish_reason)
