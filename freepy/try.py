from openai import OpenAI

openai_api_url = "http://localhost:4000"
openai_api_key = "hello"

client = OpenAI(base_url=openai_api_url, api_key=openai_api_url)

resp = client.chat.completions.create(
    model = "ollama/qwen3:32b",
    messages=[
        {"role":"system", "content":"使用繁體中文跟使用者對話。"},
        {"role":"user", "content":"早安"}
    ],
    stream=True
)

for chunk in resp:
    cd = chunk.choices[0].delta.th
    content = chunk.choices[0].
    if content:
        print(content, end="", flush=True)

