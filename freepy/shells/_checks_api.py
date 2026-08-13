"""Offline contract checks for the direct Bot/LLM interaction API."""

import threading

from agentloop import Controller, Handle
from agentloop._testing import response, wants
from llms import Bot, Engine, LLM


class ScriptedLLM(LLM):
    def __init__(self, *script):
        super().__init__(model="offline", caps={"tools": True})
        self.script = list(script)
        self.lock = threading.Lock()

    def think(self, messages, **_kwargs):
        with self.lock:
            return self.script.pop(0)

    def check(self, images, tools, tool_choice):
        return None


def direct_api(check, rejects, read_file, bundle, schemas):
    check(Engine is LLM, "Engine remains a compatibility alias for LLM")
    bot = Bot(
        ScriptedLLM(response("first"), response("final"), response("again")),
        system="Be exact.", tools=[read_file, bundle])
    check(bot.system == "Be exact."
          and set(bot.dispatch) == {"read_file", "write_file"},
          "Bot owns a validated schema and dispatch pair")
    check(rejects(lambda: Bot(LLM(), tools=([schemas[0]], {})), "same names"),
          "Bot rejects incomplete tool capabilities before start")

    first = bot.start("inspect")
    check(first.wait(timeout=1) and first.state == "waiting",
          "Bot.start returns immediately and reaches an operator boundary")
    check(rejects(lambda: bot.start("overlap"), "active Round"),
          "one mutable Bot rejects overlapping Rounds")
    competing = Controller(bot, bot.dispatch, "bypass")
    competing.start()
    check(rejects(lambda: competing.join(1), "active Round"),
          "direct Controller cannot bypass Bot Round ownership")
    first.send("summarize", finish=True)
    check(first.join(1).state == "completed",
          "the first Bot Round can finish through its Controller")

    second = bot.start("continue later")
    reached = second.wait(timeout=1) and second.state == "waiting"
    prompts = [item["content"] for item in bot.history
               if item["role"] == "user"]
    check(reached and prompts == ["inspect", "summarize", "continue later"],
          "one Bot preserves history across sequential Rounds")
    second.end()
    second.join(1)

    acting = Bot(
        ScriptedLLM(
            wants(("read-1", "read_file", '{"path":"README"}')),
            response("done")),
        tools=read_file)
    result = acting.start("use the tool", handle=Handle()).join(1)
    check(result.state == "completed"
          and result.tool_log == [(1, "read_file", {"path": "README"},
                                   "README")],
          "Bot.start executes the dispatch paired with its tool schemas")
