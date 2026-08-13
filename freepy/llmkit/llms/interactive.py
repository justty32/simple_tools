"""Interactive background-Round behavior shared by stateful Bots."""

import threading


class RoundStarter:
    """Provide ``start()`` and one-active-Round ownership for a Bot."""

    def _init_rounds(self):
        self._round_lock = threading.RLock()
        self._controller = None
        self._active_handle = None

    def start(self, instruction, *, handle=None, images=None,
              daemon=False, name=None):
        """Start one background Round and immediately return its Controller."""
        from agentloop import Controller, Handle

        with self._round_lock:
            if (self._controller is not None
                    and not self._controller.handle.done()):
                raise RuntimeError("this Bot already has an active Round")
            if self._active_handle is not None and not self._active_handle.done():
                raise RuntimeError("this Bot already has an active Round")
            if handle is None:
                handle = Handle(auto_finish=False)
            controller = Controller(
                self, self.dispatch, instruction, handle=handle, images=images)
            self._controller = controller
            try:
                return controller.start(daemon=daemon, name=name)
            except Exception:
                self._controller = None
                raise

    def _claim_round(self, handle):
        """Reserve this mutable Bot for one Handle until that Round ends."""
        with self._round_lock:
            controller_handle = (None if self._controller is None
                                 else self._controller.handle)
            if (controller_handle is not None and controller_handle is not handle
                    and not controller_handle.done()):
                raise RuntimeError("this Bot already has an active Round")
            if (self._active_handle is not None
                    and self._active_handle is not handle
                    and not self._active_handle.done()):
                raise RuntimeError("this Bot already has an active Round")
            self._active_handle = handle
