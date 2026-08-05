"""stream.py — stream=True 時回傳的串流處理器。

疊代它一次 yield 一個字元，只吐「答案」的字。同一條串流上還有另外兩種東西，
它們不會混進疊代結果，而是各自收在旁邊：

    reasoning   思考模型（deepseek-reasoner、qwen3 這類）的 reasoning_content，
                邊收邊累積，疊代到一半就可以讀
    tool_calls  模型要呼叫的工具；碎片要收完才拼得起來，所以讀它會先把串流跑完

不管是跑完、提前 close()、還是中途爆炸，已經收到的東西都會寫回對話歷史。
"""

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
        self._queue = ""
        self._done = False
        self.err = None

    def __iter__(self):
        return self

    def __next__(self):
        while not self._queue:
            if self._done:
                raise StopIteration
            try:
                chunk = next(self._response)
            except StopIteration:
                self._finish()
                raise
            except Exception as e:
                self.err = e
                self._finish()
                raise StopIteration
            if chunk.choices:
                self._eat(chunk.choices[0].delta)
        ch, self._queue = self._queue[0], self._queue[1:]
        return ch

    def _eat(self, delta):
        """拆一片 delta：思考、工具碎片各自收好，答案的字排進待吐佇列。"""
        thought = getattr(delta, "reasoning_content", None)
        if thought:
            self._reasoning += thought
        self._tools.feed(getattr(delta, "tool_calls", None))
        if delta.content:
            self._buffer += delta.content
            self._queue += delta.content

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
