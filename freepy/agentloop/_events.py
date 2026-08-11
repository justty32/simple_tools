"""事件驅動競態測試的小工具。"""

import threading
import time


def event(value, label, timeout=1):
    if not value.wait(timeout):
        raise TimeoutError(f"測試沒等到 {label}")


def until(predicate, timeout=1):
    deadline = time.monotonic() + timeout
    while not predicate():
        if time.monotonic() >= deadline:
            raise TimeoutError("測試等待狀態逾時")
        time.sleep(0.001)
