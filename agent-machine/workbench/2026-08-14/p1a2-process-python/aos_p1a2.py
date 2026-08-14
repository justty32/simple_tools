#!/usr/bin/env python3
"""Thin CLI for P1a-2 Phase 1 root acceptance and recovery."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from p1a2_model import ROOT_FAILPOINTS, Runtime
from p1a2_store import AcceptanceRejected, Corruption, MAX_JSON, StoreError, read_regular, strict_json


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--store", required=True)
    parser.add_argument("--failpoint", choices=ROOT_FAILPOINTS)
    commands = parser.add_subparsers(dest="command", required=True)
    accept = commands.add_parser("accept")
    accept.add_argument("--root-id", required=True)
    accept.add_argument("--call-file", required=True)
    commands.add_parser("recover")
    ns = parser.parse_args()
    try:
        runtime = Runtime(ns.store, failpoint=ns.failpoint)
        if ns.command == "accept":
            call_file = Path(ns.call_file)

            def resolver():
                data = read_regular(call_file, MAX_JSON, "acceptance Call input")
                assert data is not None
                return strict_json(data, "acceptance Call input")

            projection = runtime.accept_fixture_root(ns.root_id, resolver)
        else:
            projection = runtime.recover_store()
        print(json.dumps(projection.report(), ensure_ascii=False, sort_keys=True,
                         separators=(",", ":")))
        return 0
    except AcceptanceRejected as exc:
        print("accept rejected: " + str(exc), file=sys.stderr)
        return 2
    except Corruption as exc:
        print("corruption: " + str(exc), file=sys.stderr)
        return 2
    except (StoreError, OSError) as exc:
        print("runtime error: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
