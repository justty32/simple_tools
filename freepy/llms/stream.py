"""stream.py — stream=True 時回傳的串流處理器。

疊代它一次 yield 一個字元，只吐「答案」的字。同一條串流上還有另外兩種東西，
它們不會混進疊代結果，而是各自收在旁邊：

    reasoning   思考模型（deepseek-reasoner、qwen3 這類）的 reasoning_content，
                邊收邊累積，疊代到一半就可以讀
    tool_calls  模型要呼叫的工具；碎片要收完才拼得起來，所以讀它會先把串流跑完

想看著模型邊想邊印，改用 parts()，它把思考和答案一起即時吐出來：

    for kind, ch in handler.parts():   # kind 是 "think" 或 "answer"
        print(ch, end="", flush=True)

兩種疊代方式擇一，不要混用 —— 一般疊代會把路過的思考字元丟掉（完整的思考仍然
留在 reasoning 裡），parts() 則兩種都給。

不管是跑完、提前 close()、還是中途爆炸，已經收到的東西都會寫回對話歷史。
但 handler 拿了卻**完全不碰**（不疊代也不 close）就沒人收尾：這一輪的 assistant
訊息不會進歷史，連線也不會關。不確定會不會讀完就用 with。
"""

import collections

from . import toolcalls


class StreamHandler:
    """包住 openai SDK 的串流回應，逐字吐出答案，順便把思考和 tool_calls 收好。"""

    def __init__(self, response, llm, remember):
        self._response = response
        self._llm = llm
        self._remember = remember
        self._buffer = ""
        self._reasoning = ""
        self._tools = toolcalls.Accumulator()
        self._queue = collections.deque()  # [(kind, ch)]，kind 是 "think" 或 "answer"
        self._done = False
        self.err = None

    def __iter__(self):
        return self

    def __next__(self):
        while True:
            while not self._queue:
                if self._done:
                    raise StopIteration
                self._pump()
            kind, ch = self._queue.popleft()
            if kind == "answer":
                return ch

    def parts(self):
        """一次 yield 一個 (kind, ch)，kind 是 "think" 或 "answer"，兩種都即時。"""
        while True:
            while not self._queue:
                if self._done:
                    return
                self._pump()
            yield self._queue.popleft()

    def _pump(self):
        """從串流收一片 chunk 進佇列；收完或爆掉就收尾。"""
        try:
            chunk = next(self._response)
        except StopIteration:
            self._finish()
            return
        except Exception as e:
            self.err = e
            self._finish()
            return
        if chunk.choices:
            self._eat(chunk.choices[0].delta)

    def _eat(self, delta):
        """拆一片 delta：工具碎片收到 Accumulator，思考和答案的字排進佇列。"""
        thought = getattr(delta, "reasoning_content", None)
        if thought:
            self._reasoning += thought
            self._queue.extend(("think", ch) for ch in thought)
        self._tools.feed(getattr(delta, "tool_calls", None))
        if delta.content:
            self._buffer += delta.content
            self._queue.extend(("answer", ch) for ch in delta.content)

    def _drain(self):
        for _ in self:
            pass

    @property
    def text(self):
        """消費掉剩下的串流，回傳從頭到尾累積的完整答案文字。"""
        self._drain()
        return self._buffer

    @property
    def reasoning(self):
        """目前為止收到的思考內容；串流還沒跑完就是還沒收完的半成品。"""
        return self._reasoning

    @property
    def tool_calls(self):
        """模型要呼叫的工具 [{"id", "name", "args"}]，沒有就是空 list。會先跑完串流。"""
        self._drain()
        return self._tools.calls()

    def close(self):
        """提前結束串流；已經收到的東西仍會寫回歷史。"""
        self._finish()

    def _finish(self):
        if self._done:
            return
        self._done = True
        try:
            self._response.close()
        except Exception:
            pass
        if not self._remember:
            return
        if self._tools:
            # 有 tool_calls 的回合要連工具一起寫回去，下一輪送 tool 結果時 API 才對得起來
            self._llm.history.append(
                toolcalls.history_message(self._buffer or None, self._tools.raw())
            )
        else:
            self._llm.history.append({"role": "assistant", "content": self._buffer})

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
