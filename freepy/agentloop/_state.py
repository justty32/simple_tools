"""Handle 的內部狀態轉移。外部只使用 `handle.Handle`。"""

import time


class RoundState:
    def checkpoint(self):
        """在安全邊界讀控制狀態，並讓 `now()` 看得到 pause。"""
        with self._lock:
            if self.stopping:
                return "stopped"
            if self.paused:
                self.phase, self.tool = "paused", None
                return "paused"
            return "ready"

    def start_tools(self):
        """原子提交「開始這一批 tools」，避免 stop/pause 卡在縫裡。"""
        with self._lock:
            if self.stopping:
                return "stopped"
            if self.paused:
                self.phase, self.tool = "paused", None
                return "paused"
            self.phase, self.tool = "tool", None
            return "started"

    def start_step(self, settlement=False):
        """原子取走 FIFO 追加輸入並開始一個 Step。

        已執行的 tool batch 即使途中收到 stop，也必須先送完 results。
        """
        with self._lock:
            if self.stopping and not settlement:
                return "stopped", None, None, None, None
            if self.paused and not self.stopping:
                self.phase, self.tool = "paused", None
                return "paused", None, None, None, None
            self.phase, self.tool = "thinking", None
            # stop 優先於已排隊的外部輸入；settlement 只還 tool results。
            accepting = not self.stopping
            text = "\n".join(self._say) if self._say and accepting else None
            images = list(self._images) if self._images and accepting else None
            tools = list(self._tool_updates) if self._tool_updates and accepting else None
            if accepting:
                for name, value in self._ask_updates.items():
                    if value is None:
                        self._ask_options.pop(name, None)
                    else:
                        self._ask_options[name] = value
            options = dict(self._ask_options)
            self._say.clear()
            self._images.clear()
            self._tool_updates.clear()
            self._ask_updates.clear()
            return "started", text, images, tools, options

    def spoke(self, text, usage):
        with self._lock:
            self.step += 1
            self.text = text
            self.tokens += (usage or {}).get("total") or 0

    def reset_quiet(self):
        with self._lock:
            self.quiet = 0

    def finish_or_continue(self, finish_reason, quiet, nudge):
        """沒有 tool calls 時，原子決定繼續還是提交完成。"""
        with self._lock:
            if self.stopping:
                self._end_unlocked("stopped")
                return "ended"
            if finish_reason == "length":
                self._end_unlocked("length")
                return "ended"
            if self.paused:
                self.phase, self.tool = "paused", None
                return "paused"
            if self._say or self._images or self._tool_updates or self._ask_updates:
                return "continue"
            self.quiet += 1
            if self.quiet >= quiet:
                self._end_unlocked("done")
                return "ended"
            self._say.append(nudge)
            return "continue"

    def doing(self, call):
        with self._lock:
            self.phase, self.tool = "tool", call.get("name")

    def did(self, call, out, ran=True):
        """記一筆。`ran=False` 是被 policy 擋下、根本沒跑的。"""
        name = call.get("name")
        with self._lock:
            self.tool_log.append((self.step, name, call.get("args"), out))
            if ran:
                self.calls += 1
                self.used[name] = self.used.get(name, 0) + 1

    def end(self, stop, err=None):
        with self._lock:
            self._end_unlocked(stop, err)
        return self

    def _end_unlocked(self, stop, err=None):
        if self.stop is not None:
            return
        self.stop, self.err = stop, err
        self.phase, self.tool = stop, None
        self._finished = time.monotonic()
        self._say.clear()
        self._images.clear()
        self._tool_updates.clear()
        self._ask_updates.clear()
