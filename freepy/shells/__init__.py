"""Small helpers for human-facing FreePy shells."""


def session(bot, dispatch=None, prompt=None, *, handle=None, images=None,
            daemon=False, name=None):
    """Start one interactive local Round and return its Controller.

    A new Handle waits for more input after a natural answer. Supplying a
    Handle keeps all of that Handle's explicit settings unchanged.
    """
    from agentloop import Controller, Handle

    if handle is None:
        handle = Handle(auto_finish=False)
    return Controller(
        bot, dispatch, prompt, handle=handle, images=images
    ).start(daemon=daemon, name=name)


__all__ = ["session"]
