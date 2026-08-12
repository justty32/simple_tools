"""Deterministic event reducer; unknown events fail closed."""

from dataclasses import replace

from .events import Event
from .records import BotRecord, GoalRecord, GoalStatus, MachineState, RoundPhase, RoundRecord


def apply(state: MachineState, event: Event) -> MachineState:
    if event.kind == "bot_registered":
        bots = dict(state.bots)
        bots[event.object_id] = BotRecord(event.object_id, str(event.data["image"]), event.generation)
        return replace(state, bots=bots)
    if event.kind == "goal_submitted":
        goals = dict(state.goals)
        goals[event.object_id] = GoalRecord(event.object_id, str(event.data["bot_id"]),
                                             str(event.data["specification"]), generation=event.generation)
        return replace(state, goals=goals)
    if event.kind == "round_enqueued":
        rounds = dict(state.rounds)
        rounds[event.object_id] = RoundRecord(event.object_id, str(event.data["goal_id"]),
                                               str(event.data["context_ref"]),
                                               RoundPhase.RUNNABLE, event.generation)
        return replace(state, rounds=rounds)
    if event.kind == "candidate_submitted":
        round_ = state.rounds[event.object_id]
        rounds = dict(state.rounds)
        rounds[event.object_id] = replace(round_, phase=RoundPhase.CANDIDATE,
                                          candidate=str(event.data["assertion"]),
                                          generation=event.generation)
        return replace(state, rounds=rounds)
    if event.kind == "goal_verified":
        goal = state.goals[event.object_id]
        accepted = bool(event.data["accepted"])
        goals = dict(state.goals)
        goals[event.object_id] = replace(
            goal, status=GoalStatus.ACHIEVED if accepted else GoalStatus.REJECTED,
            evidence=tuple(event.data["evidence_refs"]), generation=event.generation)
        return replace(state, goals=goals)
    raise ValueError(f"unknown event kind: {event.kind}")


def replay(events):
    state = MachineState()
    for event in events:
        state = apply(state, event)
    return state
