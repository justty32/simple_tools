"""Explicit tool-directory discovery on top of :mod:`tooljson`."""

import tooljson

from .discover import ENV, Problem, ScanResult, executables, roots, scan, spec_files, spec_path


class DiscoveryError(ValueError):
    """Configured tool specs are broken, so the advertised set is incomplete."""


def tools(directories=None):
    """Discover specs and return ``(schemas, dispatch)`` like ``tooljson.tools``.

    Invalid specs fail fast.  Use :func:`scan` when a UI needs to show partial
    results and individual diagnostics.  Executables without specs are not an
    error and are never exposed to the model automatically.
    """
    result = scan(directories)
    if result.errors:
        details = "\n".join(f"- {one.path}: {one.message}" for one in result.errors)
        raise DiscoveryError(f"cannot load the configured tool catalog:\n{details}")
    return tooljson.tools(*result.specs.values())


__all__ = [
    "DiscoveryError", "ENV", "Problem", "ScanResult", "executables", "roots",
    "scan", "spec_files", "spec_path", "tools",
]
