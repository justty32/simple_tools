"""Immutable domain records for the Agent Machine pure core."""

from dataclasses import dataclass, field
from enum import Enum
from typing import Mapping


class GoalStatus(str, Enum):
    PENDING = "pending"
    ACTIVE = "active"
    VERIFYING = "verifying"
    ACHIEVED = "achieved"
    BLOCKED = "blocked"
    REJECTED = "rejected"
    FAILED = "failed"
    CANCELLED = "cancelled"


class RoundPhase(str, Enum):
    QUEUED = "queued"
    RUNNABLE = "runnable"
    PAUSED = "paused"
    CANDIDATE = "candidate"
    COMPLETED = "completed"
    FAILED = "failed"
    CANCELLED = "cancelled"


@dataclass(frozen=True)
class BotRecord:
    bot_id: str
    image: str
    generation: int = 1


@dataclass(frozen=True)
class GoalRecord:
    goal_id: str
    bot_id: str
    specification: str
    status: GoalStatus = GoalStatus.PENDING
    generation: int = 1
    evidence: tuple[str, ...] = ()


@dataclass(frozen=True)
class RoundRecord:
    round_id: str
    goal_id: str
    context_ref: str
    phase: RoundPhase = RoundPhase.QUEUED
    generation: int = 1
    candidate: str | None = None


@dataclass(frozen=True)
class MachineState:
    """Reducer output. Dictionaries are replaced, never mutated in place."""

    bots: Mapping[str, BotRecord] = field(default_factory=dict)
    goals: Mapping[str, GoalRecord] = field(default_factory=dict)
    rounds: Mapping[str, RoundRecord] = field(default_factory=dict)
