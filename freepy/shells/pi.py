#!/usr/bin/env python3
"""Enter Pi, loading FreePy's adapter when a bridge factory is configured."""

import os
from pathlib import Path
import sys


EXTENSION = (
    Path(__file__).resolve().parent.parent
    / "adapters" / "pi" / "pi-agentloop.ts"
)
FACTORY_ENV = "AGENTLOOP_PI_FACTORY"


def launcher_args(argv=None, environ=None):
    """Return Pi arguments to insert before the pass-through arguments.

    A factory is required before the extension can start an agentloop Round, so
    its existing environment variable doubles as the opt-in.  Plain
    ``python -m shells pi`` therefore keeps its historical pass-through
    behavior.
    """
    args = list(sys.argv[1:] if argv is None else argv)
    env = os.environ if environ is None else environ
    if not env.get(FACTORY_ENV) or _loads_bundled_extension(args):
        return []
    return ["-e", str(EXTENSION)]


def _loads_bundled_extension(args):
    target = _normalized(EXTENSION)
    for index, arg in enumerate(args[:-1]):
        if arg in {"-e", "--extension"} and _normalized(args[index + 1]) == target:
            return True
    return False


def _normalized(path):
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def main():
    # Imported lazily so launcher_args() remains directly testable as a package.
    from common import enter

    enter("pi", *launcher_args())


if __name__ == "__main__":
    main()
