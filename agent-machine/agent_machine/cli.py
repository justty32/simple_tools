"""Agent Machine command line entry."""

import argparse
import json
from pathlib import Path

from .new import create_function
from .runs import next_step, pause, resume, show, start, stop


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="agent-machine")
    commands = parser.add_subparsers(dest="command", required=True)

    new = commands.add_parser("new", help="create a Function directory")
    new.add_argument("path", type=Path)

    start_command = commands.add_parser("start", help="start a run")
    start_command.add_argument("bot", type=Path)
    start_command.add_argument("instruction")

    next_command = commands.add_parser("next", help="do the next step")
    next_command.add_argument("run", type=Path)

    show_command = commands.add_parser("show", help="show a run")
    show_command.add_argument("run", type=Path)

    for name in ("pause", "resume", "stop"):
        command = commands.add_parser(name, help=f"{name} a run")
        command.add_argument("run", type=Path)

    run_command = commands.add_parser("run", help="start and finish a run")
    run_command.add_argument("bot", type=Path)
    run_command.add_argument("instruction")

    args = parser.parse_args(argv)
    if args.command == "new":
        create_function(args.path)
        return 0
    if args.command == "start":
        print(start(args.bot, args.instruction))
        return 0
    if args.command == "next":
        _print_json(next_step(args.run))
        return 0
    if args.command == "show":
        _print_json(show(args.run))
        return 0
    if args.command == "pause":
        _print_json(pause(args.run))
        return 0
    if args.command == "resume":
        _print_json(resume(args.run))
        return 0
    if args.command == "stop":
        _print_json(stop(args.run))
        return 0
    if args.command == "run":
        state = next_step(start(args.bot, args.instruction))
        while state["status"] == "ready":
            state = next_step(state["handle"])
        if state["status"] == "done":
            print(state["messages"][-1]["content"])
            return 0
        return 1
    raise AssertionError(f"unhandled command: {args.command}")


def _print_json(value: object) -> None:
    print(json.dumps(value, ensure_ascii=False, indent=2))
