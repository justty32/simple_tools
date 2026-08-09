"""handle.py — 外面那條 routine 手上的把手：問狀況，也下得了指令。

`run()` 在跑的時候一路更新它，你在**另一條 routine** 裡隨時讀、隨時下指令。
讀和下指令都是普通函式，不用 await —— 所以別的 coroutine、別的 thread、
甚至一個 REPL 都問得動、按得動。

指令都是**下一輪開頭才生效**的（`say` 是下一次送出時），不會打斷正在跑的那一輪。
沒有「立刻中斷」這種東西：模型那次 HTTP 呼叫和跑到一半的工具都停不下來，
假裝停得下來只會讓人以為工具沒跑過。
"""

import time


class Handle:
    """一個 `run()` 的把手。不傳給 `run()` 也行，它會自己生一個回給你。"""

    def __init__(self):
        self.round = 0  # 模型講到第幾輪
        self.limits = None  # 這次的預算，run() 開頭掛上來
        self.phase = "idle"  # idle / thinking / tool / paused，停了就是 stop 的值
        self.tool = None  # phase 是 "tool" 時，正在跑哪一個
        self.text = ""  # 模型最後說的那段話
        self.steps = []  # 做過的事 [(第幾輪, 工具名, args, 結果字串)]，含被擋下來的
        self.calls = 0  # 真的跑掉幾個工具（被預算擋下來的不算）
        self.used = {}  # {工具名: 真的跑掉幾次}，per_tool 那條限制數的就是它
        self.quiet = 0  # 連續幾輪沒叫工具
        self.stop = None  # 為什麼停，還在跑就是 None（見 loop.py 那張表）
        self.err = None  # 停在 error / engine 時，出了什麼事
        self.tokens = 0  # 累計花掉的 token，模型每輪回報的加總
        self.paused = False
        self.stopping = False
        self._say = []
        self._started = None

    # ---- 問狀況 ----

    def done(self) -> bool:
        """收工了沒。"""
        return self.stop is not None

    def elapsed(self) -> float:
        """開跑到現在幾秒。還沒開跑是 0。"""
        return 0.0 if self._started is None else time.monotonic() - self._started

    def now(self) -> str:
        """一行人話：現在在幹嘛。印給人看的，不要拿去 parse。"""
        rounds = self.limits.rounds if self.limits else "?"
        where = f"第 {self.round}/{rounds} 輪"
        tail = f"{self.calls} 個工具，{self.tokens} tokens，{self.elapsed():.0f}s"
        if self.stop:
            why = f"{self.stop}: {self.err}" if self.err else self.stop
            return f"停了（{why}）：{where}，{tail}"
        doing = {"idle": "還沒開始", "thinking": "正在想", "paused": "暫停中"}.get(
            self.phase, f"正在跑 {self.tool}")
        return f"{where}，{doing}，{tail}"

    # ---- 下指令 ----

    def say(self, text):
        """插一句話給模型，下一次送出時跟著過去。可以連下好幾句，會照順序接起來。

        這是**改方向**用的（「別再讀了，直接寫檔」），不是聊天 —— 它跟工具結果
        一起送，模型下一輪就看到。
        """
        if text:
            self._say.append(str(text))

    def pause(self):
        """下一輪開頭停住不送出，等 `resume()`。跑到一半的那一輪會先跑完。"""
        self.paused = True

    def resume(self):
        self.paused = False

    def ask_stop(self):
        """叫它收手，`stop` 會是 `"stopped"`。暫停中的也一起放行出來。"""
        self.stopping = True

    # ---- 以下是 run() 在用的，外面不用叫 ----

    def begin(self, limits):
        self.limits = limits
        self._started = time.monotonic()

    def turn(self):
        self.round += 1
        self.phase, self.tool = "thinking", None

    def take_say(self):
        """取走外面插的話，取走就清掉。沒有就是 None（等於這一輪不多說什麼）。"""
        if not self._say:
            return None
        text = "\n".join(self._say)
        self._say.clear()
        return text

    def spoke(self, text, usage):
        self.text = text
        self.tokens += (usage or {}).get("total") or 0

    def doing(self, call):
        self.phase, self.tool = "tool", call.get("name")

    def did(self, call, out, ran=True):
        """記一筆。`ran=False` 是被預算擋下來、根本沒跑的，不能算進用量。"""
        name = call.get("name")
        self.steps.append((self.round, name, call.get("args"), out))
        if ran:
            self.calls += 1
            self.used[name] = self.used.get(name, 0) + 1

    def end(self, stop, err=None):
        self.stop, self.err = stop, err
        self.phase, self.tool = stop, None
        return self
