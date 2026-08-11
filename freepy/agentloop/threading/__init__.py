"""Optional thread convenience for the synchronous agentloop core."""

import threading as _threading

from ..handle import Handle
from ..loop import run


class BackgroundRun:
    """One ``agentloop.run()`` executing on an owned background thread."""

    def __init__(self, bot, dispatch=None, prompt=None, handle=None, images=None,
                 *, daemon=False, name=None):
        self.handle = handle or Handle()
        self.result = None
        self.error = None
        self.thread = _threading.Thread(
            target=self._invoke,
            args=(bot, dispatch, prompt, images),
            daemon=daemon,
            name=name,
        )

    def _invoke(self, bot, dispatch, prompt, images):
        try:
            self.result = run(bot, dispatch, prompt, self.handle, images)
        except BaseException as err:
            self.error = err

    def start(self):
        self.thread.start()
        return self

    def join(self, timeout=None):
        """Wait, re-raise runner failures, and return the completed Handle."""
        self.thread.join(timeout)
        if self.thread.is_alive():
            raise TimeoutError("background agentloop is still running")
        if self.error is not None:
            raise self.error
        return self.result

    def is_alive(self):
        return self.thread.is_alive()


def start(bot, dispatch=None, prompt=None, handle=None, images=None, *,
          daemon=False, name=None):
    """Start the synchronous loop in one new thread and return its controller."""
    return BackgroundRun(
        bot, dispatch, prompt, handle, images, daemon=daemon, name=name).start()


__all__ = ["BackgroundRun", "start"]
