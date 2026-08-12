"""Offline checks for the human-facing shell helpers."""

import base_tools

from llms import Engine, to_tools

from . import assistant, toolbox


def read_file(path: str) -> str:
    """Read one file."""
    return path


def write_file(path: str, content: str) -> str:
    """Write one file."""
    return content


def check(condition, label):
    if not condition:
        raise AssertionError(label)
    print(f"ok  {label}")


def rejects(call, text):
    try:
        call()
    except (TypeError, ValueError) as exc:
        return text in str(exc)
    return False


def main():
    bundle = to_tools(write_file)
    schemas, dispatch = toolbox(read_file, bundle)
    check([x["function"]["name"] for x in schemas]
          == ["read_file", "write_file"], "toolbox preserves explicit order")
    check(dispatch == {"read_file": read_file, "write_file": write_file},
          "toolbox combines callable and bundle dispatch")
    check(rejects(lambda: toolbox(read_file, to_tools(read_file)), "duplicate"),
          "toolbox rejects names that would shadow an effect")
    check(rejects(lambda: toolbox(([schemas[0]], {})), "same names"),
          "toolbox rejects schema and dispatch mismatch")

    engine = Engine(model="offline", caps={"tools": True})
    bot, dispatch = assistant(
        engine, read_file, bundle, system="Be exact.")
    check(bot.engine is engine and bot.system == "Be exact.",
          "assistant preserves explicit engine and system")
    check(bot.tools == schemas and set(dispatch) == {"read_file", "write_file"},
          "assistant returns a matched bot and dispatch")

    preset_bot, base_dispatch = assistant(
        "lm-gemma-4-12b", base_tools.tools())
    check(preset_bot.engine.model == "lm-gemma-4-12b"
          and set(base_dispatch) == {fn.__name__ for fn in base_tools.ALL},
          "documented preset and base-tools setup stays offline")
    check(rejects(lambda: assistant(object()), "Engine"),
          "assistant rejects ambiguous engine values")


if __name__ == "__main__":
    main()
