"""handle.py — 外層手上的把手：問狀況，也下得了指令。

`run()` 在 worker thread 裡跑時一路更新它，外層可從另一條 thread
隨時讀、隨時下指令。這些都是普通同步函式。
這些 live 查詢、控制、追加輸入 FIFO 和 completion commit 共用一把 lock。
公開結果在 `done()` 為真後不再變動；一個 Handle 也只能啟動一個 Round。

追加輸入都是**下一步開頭才生效**的，不會打斷正在跑的那一步。
沒有「立刻中斷」這種東西：模型那次 HTTP 呼叫和跑到一半的工具都停不下來，
假裝停得下來只會讓人以為工具沒跑過。
"""

import threading
import time

from ._inputs import PendingInputs
from ._state import RoundState


class Handle(RoundState, PendingInputs):
    """一個 `run()` 的把手。不傳給 `run()` 也行，它會自己生一個回給你。"""

    def __init__(self):
        self._lock = threading.Lock()
        self.step = 0  # 這個 Round 已經留下幾則 ask() → message
        self.limits = None  # 這次的預算，run() 開頭掛上來
        self.phase = "idle"  # idle / thinking / tool / paused，停了就是 stop 的值
        self.tool = None  # phase 是 "tool" 時，正在跑哪一個
        self.text = ""  # 模型最後說的那段話
        self.tool_log = []  # [(第幾步, 工具名, args, 結果字串)]，含被擋下來的
        self.calls = 0  # 真的跑掉幾個工具（被預算擋下來的不算）
        self.used = {}  # {工具名: 真的跑掉幾次}，per_tool 那條限制數的就是它
        self.quiet = 0  # 連續幾步沒叫工具
        self.stop = None  # 為什麼停，還在跑就是 None（見 loop.py 那張表）
        self.err = None  # 停在 error / engine 時，出了什麼事
        self.tokens = 0  # 累計花掉的 token，模型每步回報的加總
        self.paused = False
        self.stopping = False
        self._init_inputs()
        self._started = None
        self._finished = None

    # ---- 問狀況 ----

    def done(self) -> bool:
        """收工了沒。"""
        with self._lock:
            return self.stop is not None

    def elapsed(self) -> float:
        """開跑到現在幾秒。還沒開跑是 0。"""
        with self._lock:
            if self._started is None:
                return 0.0
            end = self._finished if self._finished is not None else time.monotonic()
            return end - self._started

    def now(self) -> str:
        """一行人話：現在在幹嘛。印給人看的，不要拿去 parse。"""
        with self._lock:
            steps = self.limits.steps if self.limits else "?"
            where = f"第 {self.step}/{steps} 步"
            if self._started is None:
                elapsed = 0.0
            else:
                end = self._finished if self._finished is not None else time.monotonic()
                elapsed = end - self._started
            tail = f"{self.calls} 個工具，{self.tokens} tokens，{elapsed:.0f}s"
            stop, err, phase, tool = self.stop, self.err, self.phase, self.tool
        if stop:
            why = f"{stop}: {err}" if err else stop
            return f"停了（{why}）：{where}，{tail}"
        doing = {"idle": "還沒開始", "thinking": "正在想", "paused": "暫停中"}.get(
            phase, f"正在跑 {tool}")
        return f"{where}，{doing}，{tail}"

    # ---- 下控制；追加輸入方法由 PendingInputs 提供 ----

    def pause(self):
        """下一步開頭停住不送出，等 `resume()`。跑到一半的那一步會先跑完。"""
        with self._lock:
            if self.stop is None and not self.stopping:
                self.paused = True

    def resume(self):
        with self._lock:
            self.paused = False

    def ask_stop(self):
        """叫它收手，`stop` 會是 `"stopped"`。暫停中的也一起放行出來。"""
        with self._lock:
            if self.stop is not None:
                return
            self.stopping = True
            self.paused = False

    # ---- 以下是 run() 在用的，外面不用叫 ----

    def begin(self, limits, prompt=None, images=None):
        """將 Handle 原子綁定到一個 Round；同一個 Handle 不能再用。"""
        with self._lock:
            if self._started is not None:
                raise RuntimeError("handle already used")
            self.limits = limits
            self._started = time.monotonic()
            if prompt:
                self._say.append(str(prompt))
            if images:
                images = [images] if isinstance(images, str) else images
                self._images.extend(str(image) for image in images if image)
