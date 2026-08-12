"""Offline checks for the Pi shell launcher's argument policy."""

from pathlib import Path

from shells.pi import EXTENSION, launcher_args


def check(ok, message):
    if not ok:
        raise AssertionError(message)
    print(f"ok - {message}")


def main():
    plain = ["--offline", "hello"]
    check(
        launcher_args(plain, {}) == [],
        "plain Pi inserts nothing before the argument pass-through",
    )

    configured = {"AGENTLOOP_PI_FACTORY": "project.factory:create"}
    expected = ["-e", str(EXTENSION)]
    check(
        launcher_args(plain, configured) == expected,
        "a configured bridge factory loads the bundled extension",
    )
    check(Path(launcher_args([], configured)[1]).is_absolute(),
          "the extension path is independent of the caller's directory")

    for flag in ("-e", "--extension"):
        explicit = [flag, str(EXTENSION), "--offline"]
        check(
            launcher_args(explicit, configured) == [],
            f"an explicit bundled extension via {flag} is not duplicated",
        )

    relative = str(EXTENSION.relative_to(Path.cwd()))
    explicit_relative = ["-e", relative]
    check(
        launcher_args(explicit_relative, configured) == [],
        "an equivalent relative extension path is not duplicated",
    )
    print("Pi launcher checks passed")


if __name__ == "__main__":
    main()
