"""Accepted, append-only facts emitted by the kernel."""

from dataclasses import dataclass
from typing import Mapping


@dataclass(frozen=True)
class Event:
    event_id: str
    kind: str
    object_id: str
    generation: int
    data: Mapping[str, object]
