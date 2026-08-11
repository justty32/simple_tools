"""A synchronous Step/tool driver with mutable state and boundary callbacks.

Typical use::

    h = agentloop.Handle()
    h.after_step.append(inspect_or_control)
    h.after_tools.append(rewrite_results)
    agentloop.run(bot, dispatch, "do the work", handle=h)

The core does not create threads. ``waiting`` and ``paused`` park the calling
thread until another thread calls ``resume()``. Optional conveniences live in
``agentloop.threading`` and ``agentloop.limits``.
"""

from .calling import MAX_OUTPUT, perform
from .handle import CONTINUE, END, PAUSE, Decision, Handle
from .loop import run

__all__ = [
    "CONTINUE", "PAUSE", "END", "Decision", "Handle", "MAX_OUTPUT",
    "perform", "run",
]
