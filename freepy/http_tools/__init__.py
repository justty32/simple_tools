"""A fixed-endpoint HTTP execution type for tooljson."""

import tooljson

from .body import HttpBody  # import registers `_type: http`
from .invoke import set_approver


def tools(*sources):
    """Load HTTP or mixed specs after registering the ``http`` execution type."""
    return tooljson.tools(*sources)


__all__ = ["HttpBody", "set_approver", "tools"]
