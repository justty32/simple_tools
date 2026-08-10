"""client.py — LLM class：一個 bot。

    人格    system，排在每次送出的最前面，不佔歷史
    記憶    history，對話記錄（含 assistant 的 tool_calls 和工具結果）
    能力    tools，它能開口要求哪些工具 —— 但執行不歸它管
    引擎    engine，拿什麼在想，以及那個端點做得到什麼（見 engine.py）

ask() 永遠回傳一個 Reply，絕不丟例外；錯誤在 reply.err 裡，bool(reply) 會是 False。
bot 只會說話和開口要工具，工具**是誰去跑的、跑出什麼**，由呼叫端決定後餵回來。
"""

from . import toolcalls
from .content import build_content
from .engine import Engine
from .reply import Reply


class LLM:
    """一個 bot：人格 + 記憶 + 能力 + 思考引擎。"""

    def __init__(self, engine=None, system=None, tools=None):
        self._engine = engine or Engine()
        self.system = system
        self.tools = tools  # tool schema list，見 func_schema.to_schemas
        self.history = []  # 不含 system message，送出時才在最前面補上

    @property
    def engine(self):
        """目前這顆思考引擎。改 model / params 直接動它的欄位就好。"""
        return self._engine

    def set_engine(self, engine):
        """換一顆思考引擎。人格、記憶、能力都不受影響，同一段對話接著講。"""
        self._engine = engine
        return self

    def reset(self):
        """清空對話記憶。人格、能力、引擎都不動。"""
        self.history = []

    @property
    def pending_calls(self) -> list:
        """它要求了、但你還沒把結果餵回去的工具呼叫。沒欠就是空 list。

        欠著的時候直接再 ask() 一句話（而不是給 tool_results）會被 API 打回票：
        帶 tool_calls 的 assistant message 後面一定要接上對應的 tool message。
        """
        last = self.history[-1] if self.history else {}
        if last.get("role") != "assistant":
            return []
        return toolcalls.entries(
            (tc["id"], tc["function"]["name"], tc["function"]["arguments"])
            for tc in last.get("tool_calls") or []
        )

    def _messages(self):
        msgs = []
        if self.system:
            msgs.append({"role": "system", "content": self.system})
        msgs.extend(self.history)
        return msgs

    def _extend(self, messages, extra, remember):
        """把新訊息同時加進這次要送的 messages 和（需要的話）對話記憶。"""
        messages.extend(extra)
        if remember:
            self.history.extend(extra)

    def ask(self, prompt=None, images=None, tool_results=None,
            stream=False, remember=True, tool_choice=None):
        """跟 bot 說一段話，拿回一個 Reply。

        說給它聽的東西有三種，可以同時給，會照 tool_results -> prompt 的順序排：
            prompt        一段文字
            images        本機路徑或 http(s) 網址
            tool_results  {call_id: 執行結果}，等於跟它說「你要的工具跑出這些」

        這一輪怎麼跑：
            stream        True 的話 Reply 是邊收邊填的，疊代它可以逐字看
            remember      False 就不寫進 history
            tool_choice   "auto" / "none" / "required" / {"type": "function", ...}
        """
        checkpoint = len(self.history)  # 失敗時要把這一輪寫進記憶的東西收回來
        try:
            err = self._engine.check(images, self.tools, tool_choice)
            if err is not None:
                return Reply(None, err=err)

            messages = self._messages()
            if tool_results:
                self._extend(messages, toolcalls.result_messages(tool_results), remember)
            if prompt is not None or images:
                content = build_content(prompt, images)
                self._extend(messages, [{"role": "user", "content": content}], remember)

            response = self._engine.think(
                messages, tools=self.tools, tool_choice=tool_choice, stream=stream
            )
            return Reply(response, self, remember, stream, checkpoint=checkpoint)

        except Exception as e:
            # 這一輪沒問成，就別在記憶裡留下沒人回答的問題
            del self.history[checkpoint:]
            return Reply(None, err=e)
