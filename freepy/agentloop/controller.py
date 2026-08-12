"""A small local convenience wrapper around the direct :class:`Handle` API.

``Controller`` owns no database, task queue, scheduler, or cross-process
protocol.  It is intentionally just one pleasant way to use a Handle; callers
can always keep using Handle directly or build a different wrapper.
"""

import threading

from .handle import Handle
from .loop import advance as _advance
from .loop import run as _run


class Controller:
    """Bundle one Round's inputs, Handle, and optional background runner.

    ``handle`` remains public as the explicit escape hatch for callbacks and
    advanced local control.  This class improves ergonomics, not authority or
    security.
    """

    def __init__(self, bot, dispatch=None, prompt=None, handle=None, images=None):
        self.bot = bot
        self.dispatch = dispatch
        self.prompt = prompt
        self.images = images
        self.handle = handle or Handle()
        self._runner = None
        self._mode = None
        self._lock = threading.RLock()

    @property
    def state(self):
        """The current public Handle state."""
        return self.handle.state

    def now(self):
        """Return Handle's compact human-readable status."""
        return self.handle.now()

    def advance(self):
        """Run one Step or one tool batch without blocking when parked."""
        self._claim_mode("advance")
        if self.handle.state == "idle":
            _advance(self.bot, self.dispatch, self.prompt, self.handle, self.images)
        else:
            _advance(handle=self.handle)
        return self.handle

    def run(self):
        """Run synchronously on this thread until the Round ends."""
        self._claim_mode("run")
        return _run(self.bot, self.dispatch, self.prompt, self.handle, self.images)

    def start(self, *, daemon=False, name=None):
        """Start ``run()`` in one local background thread and return ``self``."""
        with self._lock:
            self._claim_mode("background")
            if self._runner is None:
                from .threading import start
                self._runner = start(
                    self.bot, self.dispatch, self.prompt, self.handle,
                    self.images, daemon=daemon, name=name)
        return self

    def join(self, timeout=None):
        """Wait for a background Round; return its Handle."""
        if self._runner is None:
            raise RuntimeError("Controller has not been started in the background")
        return self._runner.join(timeout)

    def is_alive(self):
        return False if self._runner is None else self._runner.is_alive()

    def pause(self, safe=True):
        return self.handle.pause(safe=safe)

    def resume(self):
        return self.handle.resume()

    def end(self, safe=True, reason="ended"):
        return self.handle.end(safe=safe, reason=reason)

    def _claim_mode(self, mode):
        with self._lock:
            if self._mode is None:
                self._mode = mode
            elif self._mode != mode:
                raise RuntimeError(
                    f"Controller already uses {self._mode}; create another "
                    "Controller for a different runner style")
