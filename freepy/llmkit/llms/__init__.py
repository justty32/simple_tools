"""OpenAI-compatible thinking engines and stateful local bots.

``LLM`` owns endpoint/model/generation settings. ``Bot`` owns its LLM,
system prompt, history, and complete schema/dispatch tool capabilities::

    from llms import Bot, LLM

    bot = Bot(LLM(model="deepseek-chat"), system="請簡短回答。")
    reply = bot.ask("你好")

For an interactive background Round, use ``bot.start(instruction)`` and the
returned Controller. ``Engine`` remains an alias for ``LLM`` during migration.
"""

from .client import Bot
from .engine import Engine, LLM
from .func_schema import to_schema, to_schemas, to_tools
from .params import Params
from .presets import load_preset
from .reply import Reply
from .toolset import normalize_tools

__all__ = [
    "LLM",
    "Bot",
    "Engine",
    "Params",
    "Reply",
    "load_preset",
    "to_schema",
    "to_schemas",
    "to_tools",
    "normalize_tools",
]
