from llms import LLM

bot = LLM(model="deepseek-chat", system="用繁體中文回答使用者問題")

handler, err = bot.ask("早安", stream=True)

for text in handler:
    print(text)
