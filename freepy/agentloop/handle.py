"""Thread-aware public state and controls for one agentloop Round."""

from contextlib import contextmanager
from enum import IntEnum
import threading
import time


class Decision(IntEnum):
    """A callback's control-flow vote."""

    CONTINUE = 0
    PAUSE = 1
    END = 2


CONTINUE = Decision.CONTINUE
PAUSE = Decision.PAUSE
END = Decision.END


class _RunnerContractError(RuntimeError):
    """The caller violated one-Handle/one-runner ownership."""


class Handle:
    """Live, deliberately mutable state for one ``run()``.

    A single runner thread owns model/tool execution. Other threads may inspect or
    mutate every public field. Use ``edit()`` when a compound cross-thread update
    must be atomic.
    """

    def __init__(self, *, auto_finish=True):
        self._lock = threading.RLock()
        self._condition = threading.Condition(self._lock)
        self._runner = None
        self._started = None
        self._finished = None
        self._pause_requested = False
        self._end_requested = False
        # Which committed boundary the runner will continue from.  This records
        # structure, not a frozen decision: public tool_calls/tool_results may
        # still be edited while parked or between advance() calls.
        self._boundary = None

        # Control and callbacks.
        self.auto_finish = bool(auto_finish)
        self.after_step = []
        self.after_tools = []
        self.state = "idle"
        self.stop = None
        self.end_reason = None
        self.err = None

        # Inputs and capabilities. The runner snapshots these before an operation.
        self.bot = None
        self.history = None
        self.prompt = None
        self.images = None
        self.tools = None
        self.dispatch = {}
        self.ask_options = {}

        # Latest committed Step/tool state.
        self.reply = None
        self.message = None
        self.text = ""
        self.finish_reason = None
        self.usage = None
        self.tool_calls = []
        self.tool_results = {}

        # Reports.
        self.step = 0
        self.tool_log = []
        self.calls = 0
        self.used = {}
        self.input_tokens = 0
        self.output_tokens = 0
        self.cached_input_tokens = 0
        # Convenience report only. Policies should limit input/output separately.
        self.tokens = 0

    @property
    def phase(self):
        """Compatibility spelling for the public state."""
        return self.state

    @phase.setter
    def phase(self, value):
        self.state = value

    @contextmanager
    def edit(self):
        """Make any number of public mutations atomically."""
        with self._condition:
            yield self

    def done(self):
        with self._condition:
            return self.state in {"completed", "error"}

    def elapsed(self):
        with self._condition:
            if self._started is None:
                return 0.0
            return (self._finished or time.monotonic()) - self._started

    def now(self):
        with self._condition:
            elapsed = 0.0 if self._started is None else (
                (self._finished or time.monotonic()) - self._started)
            state, step, calls = self.state, self.step, self.calls
            input_tokens, output_tokens = self.input_tokens, self.output_tokens
            stop, err = self.stop, self.err
        if state in {"completed", "error"}:
            why = f"{stop}: {err}" if err else stop
            return (f"停了（{why}）：第 {step} 步，{calls} 個工具，"
                    f"{input_tokens} input / {output_tokens} output tokens，"
                    f"{elapsed:.0f}s")
        labels = {
            "idle": "還沒開始", "ready": "準備執行", "running_step": "正在想",
            "running_tools": "正在跑工具", "waiting": "等待繼續", "paused": "暫停中",
        }
        return (f"第 {step} 步，{labels.get(state, state)}，{calls} 個工具，"
                f"{input_tokens} input / {output_tokens} output tokens，"
                f"{elapsed:.0f}s")

    def pause(self, safe=True):
        """Pause at a boundary, or request the next one; never cut an operation."""
        if not safe:
            raise ValueError("agentloop only supports safe cooperative pause")
        with self._condition:
            if self.done():
                return False
            self._pause_requested = True
            if self.state in {"waiting", "ready"}:
                self._park_unlocked("paused")
            self._condition.notify_all()
            return True

    def resume(self):
        """Wake this Round only when it is waiting/paused (or cancel a pending pause)."""
        with self._condition:
            accepted = self._pause_requested or self.state in {"waiting", "paused"}
            if not accepted:
                return False
            self._pause_requested = False
            if self.state in {"waiting", "paused"}:
                self.state = "ready"
            self._condition.notify_all()
            return True

    def end(self, safe=True, reason="ended"):
        """End at the next safe boundary, or immediately when already at one."""
        if not safe:
            raise ValueError("agentloop only supports safe cooperative end")
        with self._condition:
            if self.done():
                return False
            self.end_reason = reason
            if self.state in {"waiting", "paused", "ready"}:
                self._end_unlocked(reason)
            else:
                self._end_requested = True
                self._condition.notify_all()
            return True

    def wait_for_state(self, *states, timeout=None):
        """Wait until the Handle reaches any requested state."""
        wanted = set(states)
        with self._condition:
            return self._condition.wait_for(lambda: self.state in wanted, timeout)

    def wait_until_paused(self, timeout=None):
        return self.wait_for_state("paused", timeout=timeout)

    # Runner-only operations follow. They remain methods so all transitions share
    # the same condition and one linearization point.

    def _begin(self, bot, dispatch, prompt, images):
        with self._condition:
            if self._runner is not None or self._started is not None:
                raise RuntimeError("handle already used")

            # Bot properties are extension points and may run arbitrary Python.
            # Snapshot all of them before claiming this one-shot Handle so a
            # broken getter/iterator cannot leave an idle-looking half-runner.
            history = getattr(bot, "history", None)
            tools = self.tools
            if tools is None:
                tools = getattr(bot, "tools", None)
            tool_calls = list(getattr(bot, "pending_calls", None) or [])
            claim_round = getattr(bot, "_claim_round", None)
            if claim_round is not None:
                claim_round(self)

            self._runner = threading.get_ident()
            self._started = time.monotonic()
            self.bot = bot
            self.history = history
            self.dispatch = dispatch
            if self.tools is None:
                self.tools = tools
            if prompt is not None:
                self.prompt = prompt
            if images is not None:
                self.images = images
            self.tool_calls = tool_calls
            self._boundary = "start"
            self.state = "ready"
            self._condition.notify_all()

    def _start_next(self):
        """Atomically claim and snapshot the next runner operation."""
        with self._condition:
            if self.done():
                return None
            if self._runner != threading.get_ident():
                raise _RunnerContractError(
                    "only the Handle's runner thread may advance it")
            if self._end_requested:
                self._end_unlocked(self.end_reason or "ended")
                return None
            if self._pause_requested:
                self._park_unlocked("paused")
                return None
            if self.state in {"waiting", "paused"}:
                return None
            if self.state != "ready":
                raise _RunnerContractError(
                    "a runner operation is already in progress")

            action = self._choose_next_unlocked()
            if action is None:
                return None
            if action == "step":
                if self.tools is not None:
                    self.bot.tools = self.tools
                payload = (self.prompt, self.images, dict(self.tool_results),
                           dict(self.ask_options))
                self._boundary = None
                self.state = "running_step"
                self.prompt = None
                self.images = None
                self.tool_results = {}
            else:
                payload = (list(self.tool_calls), dict(self.dispatch),
                           dict(self.tool_results))
                self._boundary = None
                self.state = "running_tools"
            self._condition.notify_all()
            return action, payload

    def _choose_next_unlocked(self):
        if self._boundary == "start":
            return "tools" if self.tool_calls else "step"
        if self._boundary == "after_tools":
            return "step"
        if self._boundary == "waiting":
            return "tools" if self.tool_calls else "step"
        if self._boundary == "after_step":
            if self.tool_calls:
                return "tools"
            if self.tool_results:
                return "step"
            if self.auto_finish:
                reason = self.end_reason or (
                    "length" if self.finish_reason == "length" else "done")
                self._end_unlocked(reason)
            else:
                self._boundary = "waiting"
                self._park_unlocked("waiting")
            return None
        raise RuntimeError("Handle has no committed runner boundary")

    def _commit_step(self, reply, text, calls, finish_reason, usage):
        with self._condition:
            self.reply = reply
            self.message = text
            self.text = text
            self.finish_reason = finish_reason
            self.usage = usage
            self.tool_calls = list(calls)
            self._boundary = "after_step"
            self.step += 1
            input_tokens = (usage or {}).get("prompt") or 0
            output_tokens = (usage or {}).get("completion") or 0
            cached_input_tokens = (usage or {}).get("cached") or 0
            total_tokens = (usage or {}).get("total")
            self.input_tokens += input_tokens
            self.output_tokens += output_tokens
            self.cached_input_tokens += cached_input_tokens
            self.tokens += (input_tokens + output_tokens
                            if total_tokens is None else total_tokens)
            decision = self._callbacks_unlocked(self.after_step)
            if self.state == "error":
                return "end"
            if decision == END:
                self._end_unlocked(self.end_reason or "ended", self.err)
                return "end"
            if self._end_requested:
                self._end_unlocked(self.end_reason or "ended")
                return "end"
            if decision == PAUSE or self._pause_requested or self.state == "paused":
                self._park_unlocked("paused")
                return "parked"
            if self.tool_calls or self.tool_results:
                self.state = "ready"
                self._condition.notify_all()
                return "ready"
            if self.auto_finish:
                reason = self.end_reason or (
                    "length" if self.finish_reason == "length" else "done")
                self._end_unlocked(reason)
                return "end"
            self._boundary = "waiting"
            self._park_unlocked("waiting")
            return "parked"

    def _commit_tools(self, calls, results, records):
        with self._condition:
            self.tool_calls = list(calls)
            self.tool_results = dict(results)
            self._boundary = "after_tools"
            for call, out, ran in records:
                name = call.get("name")
                self.tool_log.append((self.step, name, call.get("args"), out))
                if ran:
                    self.calls += 1
                    self.used[name] = self.used.get(name, 0) + 1
            decision = self._callbacks_unlocked(self.after_tools)
            if self.state == "error":
                return False
            if self.state == "running_tools":
                self.state = "ready"
            if decision == END:
                self._end_unlocked(self.end_reason or "ended", self.err)
                return False
            if self._end_requested:
                self._end_unlocked(self.end_reason or "ended")
                return False
            if decision == PAUSE or self._pause_requested or self.state == "paused":
                return self._park_unlocked("paused")
            return True

    def _callbacks_unlocked(self, callbacks):
        decision = CONTINUE
        first_error = None
        for callback in list(callbacks):
            try:
                vote = callback(self)
                if vote is None:
                    vote = CONTINUE
                vote = Decision(vote)
                decision = max(decision, vote)
            except Exception as exc:
                first_error = first_error or exc
        if first_error is not None:
            self._end_unlocked("error", first_error)
            return END
        return decision

    def _park_unlocked(self, state):
        self.state = state
        self._pause_requested = state == "paused"
        self._condition.notify_all()
        return True

    def _wait_until_ready(self):
        """Runner-only blocking half of parked-runner compatibility mode."""
        with self._condition:
            while self.state in {"waiting", "paused"}:
                self._condition.wait()
                if self.done():
                    return False
            return not self.done()

    def _fail(self, err):
        with self._condition:
            # Endpoint failure discovered after a partial committed message wins
            # over a simultaneous normal completion/callback END.
            self.stop = "error"
            self.err = err
            self.state = "error"
            self._finished = time.monotonic()
            self._condition.notify_all()
            return self

    def _end_unlocked(self, stop, err=None):
        if self.state in {"completed", "error"}:
            return
        self.stop = stop
        self.err = err
        self._pause_requested = False
        self._end_requested = False
        self.state = "error" if err is not None else "completed"
        self._finished = time.monotonic()
        self._condition.notify_all()
