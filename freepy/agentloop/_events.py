"""事件驅動競態測試的 thread helper。"""

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


def background(fn, *args, **kwargs):
    box = {}

    def invoke():
        try:
            box["result"] = fn(*args, **kwargs)
        except BaseException as err:
            box["error"] = err

    thread = threading.Thread(target=invoke)
    thread.start()
    return thread, box


def finish(thread, box, timeout=2):
    thread.join(timeout)
    if thread.is_alive():
        raise TimeoutError("背景 run 沒有結束")
    if "error" in box:
        raise box["error"]
    return box["result"]
