"""Typed requests. Commands express intent; only accepted events change state."""

from dataclasses import dataclass


@dataclass(frozen=True)
class RegisterBot:
    image: str
    operation_id: str


@dataclass(frozen=True)
class SubmitGoal:
    bot_id: str
    specification: str
    operation_id: str
    expected_generation: int | None = None


@dataclass(frozen=True)
class EnqueueRound:
    goal_id: str
    context_ref: str
    operation_id: str
    expected_generation: int | None = None


@dataclass(frozen=True)
class SubmitCandidate:
    round_id: str
    assertion: str
    operation_id: str
    expected_generation: int


@dataclass(frozen=True)
class VerifyGoal:
    goal_id: str
    accepted: bool
    evidence_refs: tuple[str, ...]
    operation_id: str
    expected_generation: int
