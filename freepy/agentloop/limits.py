"""limits.py — 這次 run() 最多能花掉多少。

一個 agent 放著自己跑，唯一擋得住它的就是預算。這裡放的是**迴圈自己數得出來的**
那幾種：跑幾步、叫幾次工具、哪些工具、花多少 token、跑多久。

**作業系統那一層的（cpu / gpu / 記憶體 / 網路 / 能碰哪些檔案）不在這裡**，那要
cgroup、rlimit、容器，不是數數字就有的 —— 規劃在 [LIMITS.md](LIMITS.md)，還沒做。
沒做的東西不放進這個 class 假裝有：欄位設得下去卻沒人擋，比沒有這個欄位更糟。

兩種擋法，分得很清楚：

    停整個 agent      預算真的沒了（步數、時間、token、總呼叫數）
    回一句話給模型    只是這個工具不能用（不在白名單、這支用完了）

第二種**不是錯誤**，是情報：模型讀到「run_shell 你已經用滿 5 次了」會換一個方法
繼續做事，比整條停掉有用得多。
"""


class Limits:
    """一次 run() 的預算。全部都是上限，`None` 就是不設限。"""

    def __init__(self, steps=12, calls=None, per_tool=None, tools=None,
                 engines=None, seconds=None, tokens=None, quiet=1):
        #: 模型最多走幾步。這是唯一有預設值的 —— 沒有它，壞掉的模型會永遠跑下去
        self.steps = steps
        #: 工具總共最多叫幾次
        self.calls = calls
        #: 指定某支工具最多叫幾次 `{"run_shell": 5}`
        self.per_tool = dict(per_tool or {})
        #: 只准用這幾支工具。`None` 是 dispatch 裡的都能用
        self.tools = set(tools) if tools is not None else None
        #: 只准用這幾顆思考引擎（model 名字，就是 proxy 上的 alias）。`None` 是不管
        self.engines = set(engines) if engines is not None else None
        #: 整個 run() 最多跑幾秒（牆上時間，不是 CPU 時間）
        self.seconds = seconds
        #: 最多花掉多少 token，模型每步自己回報的加總
        self.tokens = tokens
        #: 連續幾步不叫工具就當它講完了。預設 1 = 一不叫工具就收工。
        #: 調大就會推它一把再給幾次機會 —— 小模型很常直接講一段話而不動手
        self.quiet = max(1, quiet)

    def exhausted(self, h):
        """預算沒了嗎？回停的原因，還能跑就 `None`。每步開頭問一次。"""
        if self.seconds is not None and h.elapsed() >= self.seconds:
            return "time"
        if self.tokens is not None and h.tokens >= self.tokens:
            return "tokens"
        if self.calls is not None and h.calls >= self.calls:
            return "calls"
        return None

    def engine_ok(self, bot):
        """這顆思考引擎准不准用。准就 `None`，不准就回一句話（會變成停下來的理由）。

        每步開頭都問一次，不是只問開頭那一次 —— 引擎是 `bot.engine.model` 一個
        欄位，中途換掉是這個 repo 明講支援的用法（見 llms 的 USAGE.md）。

        問不出模型名字時**擋下來，不放行**：限制設了卻悄悄不成立，比一開始就沒設
        更糟 —— 你會以為它被關在 deepseek-chat 裡。
        """
        if self.engines is None:
            return None
        model = getattr(getattr(bot, "engine", None), "model", None)
        if not model:
            return "問不出這個 bot 在用哪顆引擎，engines 這條限制沒辦法成立"
        if model not in self.engines:
            return f"引擎 {model} 不在准用的清單裡：{sorted(self.engines)}"
        return None

    def allow(self, name, h):
        """這支工具現在還叫得動嗎？叫不動就回**一句寫給模型看的話**，可以就 `None`。"""
        if self.tools is not None and name not in self.tools:
            allowed = ", ".join(sorted(self.tools)) or "(none)"
            return (f"Error: {name} is not available for this task. "
                    f"You may only use: {allowed}")
        cap = self.per_tool.get(name)
        if cap is not None and h.used.get(name, 0) >= cap:
            return (f"Error: {name} has already been used {cap} times, which is its limit "
                    f"for this task. Finish with the other tools.")
        if self.calls is not None and h.calls >= self.calls:
            return (f"Error: the tool budget for this task ({self.calls} calls) is used up. "
                    f"Answer with what you already know.")
        return None

    def __repr__(self):
        on = {k: v for k, v in vars(self).items() if v is not None and v != {}}
        return f"Limits({', '.join(f'{k}={v!r}' for k, v in on.items())})"
