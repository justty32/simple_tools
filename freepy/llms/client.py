"""client.py — LLM class：對著 OpenAI 相容 proxy 講話的精簡包裝。

一個 instance 就是一段對話，預設記住歷史。所有對外的方法都回傳
(result, err) 這種 Go 風格的 tuple，絕不丟例外。
"""

from openai import OpenAI

from . import toolcalls
from .caps import clear_cache as caps_clear, lookup as caps_lookup
from .content import build_content, normalize_base_url, resolve_key, root_url
from .stream import StreamHandler


class LLM:
    """對著 OpenAI 相容 proxy（預設本機 LiteLLM）講話的精簡包裝，本身就是一段對話。"""

    def __init__(self, url="http://localhost:4000", model="deepseek-chat",
                 key=None, system=None, params=None, timeout=60, caps=None):
        self.model = model
        self.system = system
        self.params = params
        self.caps = caps or {}
        self.history = []  # 不含 system message，送出時才在最前面補上
        self.last_reasoning = None  # 上一次非串流回答的思考內容，沒有就是 None
        self._root_url = root_url(url)
        self._key = resolve_key(key)
        self._client = OpenAI(
            base_url=normalize_base_url(url),
            api_key=self._key,
            timeout=timeout,
        )

    def reset(self):
        """清空對話歷史，system prompt 不受影響。"""
        self.history = []

    @staticmethod
    def clear_caps_cache():
        """清空 model capability 的快取，改完 litellm.yaml 重啟 proxy 後可以呼叫這個強制重查。"""
        caps_clear()

    def _caps_for(self, model):
        """回傳 {"tools", "vision", "reasoning"}，self.caps 給的值優先，其次問 proxy。"""
        return caps_lookup(self._root_url, self._key, model, self.caps)

    @property
    def supports_tools(self):
        """這個 instance 目前的 model 支不支援 tool calling：True / False / None（不知道）。"""
        return self._caps_for(self.model)["tools"]

    @property
    def supports_vision(self):
        """這個 instance 目前的 model 支不支援看圖：True / False / None（不知道）。"""
        return self._caps_for(self.model)["vision"]

    @property
    def supports_reasoning(self):
        """這個 instance 目前的 model 會不會思考：True / False / None（不知道）。

        純粹是情報，不擋任何呼叫：思考不是送出去的參數，是模型自己的事。
        會思考才值得去讀 last_reasoning / StreamHandler.reasoning。
        """
        return self._caps_for(self.model)["reasoning"]

    def _reject(self, model, images, tools):
        """能力不足就回傳一個 ValueError，可以用就回傳 None。明確 False 才擋，None 一律放行。"""
        if not images and not tools:
            return None  # 沒有要檢查的東西，就別為了純文字問答去問 proxy
        caps = self._caps_for(model)
        if images and caps["vision"] is False:
            return ValueError(f"模型 {model} 不支援圖片輸入")
        if tools and caps["tools"] is False:
            return ValueError(f"模型 {model} 不支援 tool calling")
        return None

    def _messages(self):
        msgs = []
        if self.system:
            msgs.append({"role": "system", "content": self.system})
        msgs.extend(self.history)
        return msgs

    def _extend(self, messages, extra, remember):
        """把新訊息同時加進這次要送的 messages 和（需要的話）對話歷史。"""
        messages.extend(extra)
        if remember:
            self.history.extend(extra)

    def ask(self, prompt=None, stream=False, images=None, tools=None,
            tool_results=None, remember=True, model=None, params=None):
        """送出一則訊息，永遠回傳 (result, err) 這種 Go 風格的 tuple，絕不丟例外。

        result 有三種：stream=True 給 StreamHandler、模型要叫工具給 calls list、
        其餘給答案字串。思考模型的思考內容不會混進答案裡，非串流放在 self.last_reasoning，
        串流放在 StreamHandler.reasoning。

        params 有給的話會整包取代 self.params（不是逐欄位合併）。
        """
        self.last_reasoning = None
        checkpoint = len(self.history)  # 失敗時要把這一輪寫進歷史的東西收回來
        try:
            effective_model = model or self.model

            err = self._reject(effective_model, images, tools)
            if err is not None:
                return None, err

            messages = self._messages()
            if tool_results:
                self._extend(messages, toolcalls.result_messages(tool_results), remember)
            if prompt is not None or images:
                content = build_content(prompt, images)
                self._extend(messages, [{"role": "user", "content": content}], remember)

            kwargs = {}
            p = params if params is not None else self.params
            if p is not None:
                kwargs.update(p.to_kwargs())
            if tools:
                kwargs["tools"] = tools
            # 這三個是這個 class 在管的東西，最後才寫，確保蓋得過 params.extra
            kwargs["model"] = effective_model
            kwargs["messages"] = messages
            kwargs["stream"] = stream

            response = self._client.chat.completions.create(**kwargs)

            if stream:
                return StreamHandler(response, self, remember), None

            msg = response.choices[0].message
            # 思考內容只留在自己手上，不寫回歷史：deepseek 這類 API 不收回傳的 reasoning_content
            self.last_reasoning = getattr(msg, "reasoning_content", None) or None

            if msg.tool_calls:
                if remember:
                    self.history.append(toolcalls.to_history(msg))
                return toolcalls.to_calls(msg), None

            text = msg.content or ""
            if remember:
                self.history.append({"role": "assistant", "content": text})
            return text, None

        except Exception as e:
            # 這一輪沒問成，就別在歷史裡留下沒人回答的問題
            del self.history[checkpoint:]
            return None, e
