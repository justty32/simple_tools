import llms

bot = llms.LLM(model="deepseek-reasoner", system="用繁體中文回答使用者問題")

handler, err = bot.ask("早安", stream=True)

# parts() 把思考和答案一起即時吐出來，kind 是 "think" 或 "answer"
mode = None
for kind, text in handler.parts():
    if kind != mode:
        print(f"\n[{kind}] ", end="", flush=True)
        mode = kind
    print(text, end="", flush=True)

print("\nerr:", handler.err or err)
