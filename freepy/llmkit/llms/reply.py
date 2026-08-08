"""reply.py — 一個回合的結果。ask() 永遠回傳這個，不管有沒有串流。

bot 說了什麼（text）、要你去做什麼（calls）、想了什麼（reasoning），加上後設：為什麼停
（finish_reason）、花了多少（usage）、壞了沒（err）。串流與否是同一種東西，非串流的
Reply 建好就填滿，疊代它照樣逐字吐 —— 呼叫端不用寫兩套。

收尾（寫回歷史、關連線）在跑完、close()、或中途爆炸時發生。串流的 Reply 拿了
卻完全不碰就沒人收尾，不確定會不會讀完就用 with。
"""

import collections

from . import toolcalls
from .usage import usage_dict


class Reply:
    """一個回合。err 不是 None 時 bool(reply) 是 False，其餘欄位都是空的。"""

    def __init__(self, response, llm=None, remember=False, stream=False,
                 err=None, checkpoint=0):
        self._response = response
        self._llm = llm
        self._remember = remember
        self._checkpoint = checkpoint  # 這一輪開始時 history 多長，整輪落空時退回這裡
        self._buffer = ""
        self._reasoning = ""
        self._raw_calls = []
        self._acc = toolcalls.Accumulator()
        self._queue = collections.deque()  # [(kind, ch)]，kind 是 "think" 或 "answer"
        self._done = err is not None  # 建構時就失敗的 Reply 沒東西可收
        self.err = err
        self.finish_reason = None
        self.usage = None
        if not self._done and not stream:
            self._absorb(response)
            self._finish()

    def __bool__(self):
        return self.err is None

    def __iter__(self):
        return self

    def __next__(self):
        """逐字吐答案，路過的思考字元丟掉（完整思考仍在 reasoning 裡）。"""
        for kind, ch in self.parts():
            if kind == "answer":
                return ch
        raise StopIteration

    def parts(self):
        """一次 yield 一個 (kind, ch)，kind 是 "think" 或 "answer"，兩種都給。"""
        while True:
            while not self._queue:
                if self._done:
                    return
                self._pump()
            yield self._queue.popleft()

    @property
    def text(self) -> str:
        """完整答案。串流的話會先把剩下的收完。"""
        for _ in self:
            pass
        return self._buffer

    @property
    def reasoning(self):
        """思考內容，沒有就是 None。不寫回歷史（API 不收），串流讀一半就是半成品。"""
        return self._reasoning or None

    @property
    def calls(self) -> list:
        """要你去執行的工具 [{"id", "name", "args"}]。串流的話會先把剩下的收完。"""
        self.text  # 碎片收完才拼得回完整的呼叫
        return toolcalls.entries(self._raw_calls)

    def close(self):
        """提前結束；已經收到的東西仍會寫回歷史。"""
        self._finish()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def _eat(self, thought, content):
        """新收到的思考和答案排進佇列。兩條路（串流與否）都走這裡。"""
        self._reasoning += thought or ""
        self._buffer += content or ""
        self._queue.extend(("think", ch) for ch in thought or "")
        self._queue.extend(("answer", ch) for ch in content or "")

    def _absorb(self, response):
        """非串流：一次把整包填進來。"""
        choice = response.choices[0]
        msg = choice.message
        self.finish_reason = choice.finish_reason
        self.usage = usage_dict(getattr(response, "usage", None))
        self._eat(getattr(msg, "reasoning_content", None), msg.content)
        self._raw_calls = toolcalls.raw_from(msg)

    def _pump(self):
        """串流：收一片 chunk 進佇列；收完或爆掉就收尾。"""
        try:
            chunk = next(self._response)
        except StopIteration:
            self._finish()
            return
        except Exception as e:
            self.err = e
            self._finish()
            return
        try:
            # 開了 include_usage 的話，最後一片只帶 usage、沒有 choices
            self.usage = usage_dict(getattr(chunk, "usage", None)) or self.usage
            choices = getattr(chunk, "choices", None)
            if not choices:
                return
            self.finish_reason = choices[0].finish_reason or self.finish_reason
            delta = choices[0].delta
            self._acc.feed(getattr(delta, "tool_calls", None))
            self._eat(getattr(delta, "reasoning_content", None),
                      getattr(delta, "content", None))
        except Exception as e:
            # 後端回了預期外的形狀。這裡是 ask() 的 try 管不到的（錯誤在它回來之後才發生）
            self.err = e
            self._finish()

    def _finish(self):
        if self._done:
            return
        self._done = True
        try:
            self._response.close()
        except Exception:
            pass
        if self._acc:
            self._raw_calls = self._acc.raw()
        if self._llm is None or not self._remember:
            return
        if not self._buffer and not self._raw_calls and self.finish_reason is None:
            # 整輪落空（還沒開口就斷線，或被提前 close）：這輪送出去的訊息一起收回來
            del self._llm.history[self._checkpoint:]
            return
        # 有 tool_calls 的回合要連工具一起寫回去，下一輪送 tool 結果才對得起來
        self._llm.history.append(toolcalls.history_message(self._buffer, self._raw_calls))
