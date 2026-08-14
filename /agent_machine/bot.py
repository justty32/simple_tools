"""Load the three files that make a Bot."""

from dataclasses import dataclass
from pathlib import Path

from . import jsonx


@dataclass(frozen=True)
class Bot:
    path: Path
    llm: dict
    messages: list
    tools: list


def load_bot(path: str | Path) -> Bot:
    root = Path(path).resolve()
    if not root.is_dir():
        raise NotADirectoryError(root)
    llm = jsonx.read(root / "llm.json")
    messages = jsonx.read(root / "messages.json")
    tools = jsonx.read(root / "tools.json")
    if not isinstance(llm, dict):
        raise ValueError("llm.json 頂層必須是 object")
    if not isinstance(messages, list):
        raise ValueError("messages.json 頂層必須是 array")
    if not isinstance(tools, list):
        raise ValueError("tools.json 頂層必須是 array")
    return Bot(root, llm, messages, tools)
