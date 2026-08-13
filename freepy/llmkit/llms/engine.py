"""engine.py — LLM 思考引擎：端點、模型、旋鈕與能力。

Bot 的人格（system）和記憶（history）在 client.py，這裡只管「它拿什麼在想」。

能力（caps）掛在這裡而不是掛在 bot 上，因為「能不能看圖」是端點加模型的性質，
跟這個 bot 是誰無關。答案有三種：True、False、None（proxy 沒說，就是不知道）。
"""

from openai import OpenAI

from .caps import FIELDS, clear_cache, lookup
from .content import normalize_base_url, resolve_key, root_url
from .params import Params


class LLM:
    """一顆思考引擎；改 model / params 可直接指派欄位。"""

    def __init__(self, model="deepseek-chat", url="http://localhost:4000",
                 key=None, params=None, timeout=60, caps=None):
        self.model = model
        self.params = params or Params()
        self.caps_override = caps or {}
        unknown = set(self.caps_override) - set(FIELDS)
        if unknown:
            # 打錯字的覆寫會被安靜丟掉，跟 litellm 的 drop_params 一樣難查，寧可現在就爆
            raise ValueError(f"不認得的能力名稱 {sorted(unknown)}，可用的是 {sorted(FIELDS)}")
        self._root_url = root_url(url)
        self._key = resolve_key(key)
        self._client = OpenAI(
            base_url=normalize_base_url(url),
            api_key=self._key,
            timeout=timeout,
        )

    @staticmethod
    def clear_caps_cache():
        """清掉能力快取，改完 litellm.yaml 重啟 proxy 後呼叫這個強迫重查。"""
        clear_cache()

    @property
    def caps(self) -> dict:
        """目前這顆模型的完整能力表，欄位名見 caps.FIELDS。"""
        return lookup(self._root_url, self._key, self.model, self.caps_override)

    def supports(self, name):
        """單一能力：True / False / None（proxy 沒說）。名字打錯是 KeyError，不會裝作不知道。"""
        return self.caps[name]

    def check(self, images, tools, tool_choice):
        """能力明確不足就回一個 ValueError，可以用就回 None。None（不知道）一律放行。"""
        if tool_choice and not tools:
            # 沒有 tools 就送不出 tool_choice，但與其安靜吞掉，不如講出來
            return ValueError("給了 tool_choice，但這個 bot 沒有 tools，不會有任何效果")
        if not images and not tools:
            return None  # 純文字問答沒什麼好擋的，別為了它去問 proxy
        caps = self.caps
        if images and caps["vision"] is False:
            return ValueError(f"模型 {self.model} 不支援圖片輸入")
        if tools and caps["tools"] is False:
            return ValueError(f"模型 {self.model} 不支援 tool calling")
        if tool_choice and caps["tool_choice"] is False:
            return ValueError(f"模型 {self.model} 不支援指定 tool_choice")
        return None

    def think(self, messages, tools=None, tool_choice=None, stream=False):
        """實際打出去，回傳 SDK 的原始回應。這個會丟例外，由呼叫端接。"""
        kwargs = dict(self.params.to_kwargs())
        if tools:
            kwargs["tools"] = tools
            if tool_choice:
                kwargs["tool_choice"] = tool_choice
        if stream:
            # 不開這個，串流回來的 usage 永遠是 None（最後那片才帶 usage）
            kwargs["stream_options"] = {"include_usage": True}
        # 這三個是引擎自己在管的，最後才寫，確保蓋得過 params.extra
        kwargs["model"] = self.model
        kwargs["messages"] = messages
        kwargs["stream"] = stream
        return self._client.chat.completions.create(**kwargs)


# The old name remains importable while callers migrate to the clearer model:
# LLM is the thinking engine; Bot owns identity, history, and tools.
Engine = LLM
