"""Thread-safe in-memory event store with operation idempotency."""
import threading
from .reducer import apply
from .records import MachineState

class InMemoryStore:
    def __init__(self):
        self._lock=threading.RLock(); self._state=MachineState(); self._events=[]; self._operations={}
    def transact(self, operation_id, events):
        with self._lock:
            if operation_id in self._operations: return self._operations[operation_id]
            state=self._state
            for event in events: state=apply(state,event)
            self._state=state; self._events.extend(events); self._operations[operation_id]=events
            return events
    def snapshot(self):
        with self._lock: return self._state
    def events(self):
        with self._lock: return tuple(self._events)
