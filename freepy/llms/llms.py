"""llms.py — 包一層薄薄的殼在 openai SDK 外面，對著本機的 LiteLLM proxy 講話。

一個 LLM instance 就是一段對話：預設會記住歷史，呼叫 ask() 不用自己組 messages。
只做這個 class 需要的事，不做重試、不做 logging、不做 config 檔、不做 CLI。

用法：
    from llms import LLM, Params

    bot = LLM(model="ollama-qwen3-32b", params=Params(temperature=0.2, max_tokens=200))
    reply, err = bot.ask("你好")

    if bot.supports_vision:          # True / False / None（不知道）
        bot.ask("看圖", images=["x.png"])
    if bot.supports_tools is False:  # 明確不支援才會擋，None 一律放行
        ...
"""

import base64
import json
import mimetypes
import os
import urllib.request
from dataclasses import dataclass, field

from openai import OpenAI


@dataclass
class Params:
    """API 呼叫參數，只吐出有設定的欄位。"""

    temperature: float | None = None
    top_p: float | None = None
    max_tokens: int | None = None
    seed: int | None = None
    stop: str | list | None = None
    presence_penalty: float | None = None
    frequency_penalty: float | None = None
    extra: dict = field(default_factory=dict)

    def to_kwargs(self) -> dict:
        """只吐出有設定的欄位；extra 裡的東西直接併進去。"""
        kwargs = {
            "temperature": self.temperature,
            "top_p": self.top_p,
            "max_tokens": self.max_tokens,
            "seed": self.seed,
            "stop": self.stop,
            "presence_penalty": self.presence_penalty,
            "frequency_penalty": self.frequency_penalty,
        }
        kwargs = {k: v for k, v in kwargs.items() if v is not None}
        kwargs.update(self.extra)
        return kwargs


def _normalize_base_url(url: str) -> str:
    """url 可以是 base url 或完整的 /chat/completions 端點，統一轉成 OpenAI() 要的 base_url。"""
    url = url.rstrip("/")
    if url.endswith("/chat/completions"):
        url = url[: -len("/chat/completions")]
    return url.rstrip("/")


def _resolve_key(key):
    """key 沒給就吃環境變數 OPENAI_API_KEY，再沒有就用 "hello" 頂著（本機 proxy 不檢查）。"""
    if key is not None:
        return key
    return os.environ.get("OPENAI_API_KEY") or "hello"


def _encode_image(path_or_url: str) -> dict:
    """把一張圖片（本機路徑或 http(s) URL）轉成 OpenAI 的 image_url content part。"""
    if path_or_url.startswith("http://") or path_or_url.startswith("https://"):
        url = path_or_url
    else:
        mime, _ = mimetypes.guess_type(path_or_url)
        mime = mime or "image/png"
        with open(path_or_url, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("ascii")
        url = f"data:{mime};base64,{b64}"
    return {"type": "image_url", "image_url": {"url": url}}


def _build_content(prompt, images):
    """没有圖片就送純文字字串；有圖片就組成 content-parts 的 list。"""
    if not images:
        return prompt
    parts = [{"type": "text", "text": prompt or ""}]
    parts.extend(_encode_image(img) for img in images)
    return parts


class _StreamHandler:
    """stream=True 時回傳的串流處理器：一次 yield 一個字元，結束時把完整文字寫回歷史。"""

    def __init__(self, response, llm, remember):
        self._response = response
        self._llm = llm
        self._remember = remember
        self._buffer = ""
        self._queue = ""
        self._done = False
        self.err = None

    def __iter__(self):
        return self

    def __next__(self):
        while not self._queue:
            if self._done:
                raise StopIteration
            try:
                chunk = next(self._response)
            except StopIteration:
                self._finish()
                raise
            except Exception as e:
                self.err = e
                self._finish()
                raise StopIteration
            delta = None
            if chunk.choices:
                delta = chunk.choices[0].delta.content
            if delta:
                self._buffer += delta
                self._queue += delta
        ch, self._queue = self._queue[0], self._queue[1:]
        return ch

    @property
    def text(self):
        """消費掉剩下的串流，回傳從頭到尾累積的完整文字。"""
        for _ in self:
            pass
        return self._buffer

    def close(self):
        """提前結束串流；已經收到的文字仍會寫回歷史。"""
        self._finish()

    def _finish(self):
        if self._done:
            return
        self._done = True
        try:
            self._response.close()
        except Exception:
            pass
        if self._remember:
            self._llm.history.append({"role": "assistant", "content": self._buffer})

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class LLM:
    """對著 OpenAI 相容 proxy（預設本機 LiteLLM）講話的精簡包裝，本身就是一段對話。"""

    _caps_cache = {}  # {root_url: {model_name: {"tools": bool|None, "vision": bool|None}}}

    def __init__(self, url="http://localhost:4000", model="ollama-gemma3-1b",
                 key=None, system=None, params=None, timeout=60, caps=None):
        self.model = model
        self.system = system
        self.params = params
        self.caps = caps or {}
        self.history = []  # 不含 system message，送出時才在最前面補上
        self._root_url = _normalize_base_url(url)
        if self._root_url.endswith("/v1"):
            self._root_url = self._root_url[: -len("/v1")]
        self._key = _resolve_key(key)
        self._client = OpenAI(
            base_url=_normalize_base_url(url),
            api_key=self._key,
            timeout=timeout,
        )

    def reset(self):
        """清空對話歷史，system prompt 不受影響。"""
        self.history = []

    @classmethod
    def clear_caps_cache(cls):
        """清空 model capability 的快取，改完 litellm.yaml 重啟 proxy 後可以呼叫這個強制重查。"""
        cls._caps_cache = {}

    def _model_caps(self, model):
        """回傳指定 model 的 {"tools": bool|None, "vision": bool|None}，caps 參數優先，其次查 proxy。"""
        cached = self._caps_cache.get(self._root_url)
        if cached is None:
            cached = self._fetch_caps(self._root_url, self._key)
            self._caps_cache[self._root_url] = cached
        remote = cached.get(model, {})
        return {
            "tools": self.caps.get("tools", remote.get("tools")),
            "vision": self.caps.get("vision", remote.get("vision")),
        }

    @staticmethod
    def _fetch_caps(root_url, key):
        """查 LiteLLM proxy 的 /model/info，失敗一律吞掉回傳空 dict，絕不丟例外。"""
        result = {}
        try:
            req = urllib.request.Request(
                root_url + "/model/info",
                headers={"Authorization": f"Bearer {key}"},
            )
            with urllib.request.urlopen(req, timeout=5) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            for entry in data.get("data", []):
                name = entry.get("model_name")
                info = entry.get("model_info", {})
                if name is None:
                    continue
                result[name] = {
                    "tools": info.get("supports_function_calling"),
                    "vision": info.get("supports_vision"),
                }
        except Exception:
            return {}
        return result

    def _caps_for(self, model):
        caps = self._model_caps(model)
        return caps.get("tools"), caps.get("vision")

    @property
    def supports_tools(self):
        """這個 instance 目前的 model 支不支援 tool calling：True / False / None（不知道）。"""
        tools, _ = self._caps_for(self.model)
        return tools

    @property
    def supports_vision(self):
        """這個 instance 目前的 model 支不支援看圖：True / False / None（不知道）。"""
        _, vision = self._caps_for(self.model)
        return vision

    def _messages(self):
        msgs = []
        if self.system:
            msgs.append({"role": "system", "content": self.system})
        msgs.extend(self.history)
        return msgs

    def ask(self, prompt=None, stream=False, images=None, tools=None,
            tool_results=None, remember=True, model=None, params=None):
        """送出一則訊息，永遠回傳 (result, err) 這種 Go 風格的 tuple，絕不丟例外。

        params 有給的話會整包取代 self.params（不是逐欄位合併）。
        """
        try:
            effective_model = model or self.model

            if images:
                _, vision = self._caps_for(effective_model)
                if vision is False:
                    return None, ValueError(f"模型 {effective_model} 不支援圖片輸入")
            if tools:
                supports, _ = self._caps_for(effective_model)
                if supports is False:
                    return None, ValueError(f"模型 {effective_model} 不支援 tool calling")

            messages = self._messages()

            if tool_results:
                for call_id, result in tool_results.items():
                    tool_msg = {
                        "role": "tool",
                        "tool_call_id": call_id,
                        "content": str(result),
                    }
                    messages.append(tool_msg)
                    if remember:
                        self.history.append(tool_msg)

            if prompt is not None:
                user_msg = {"role": "user", "content": _build_content(prompt, images)}
                messages.append(user_msg)
                if remember:
                    self.history.append(user_msg)

            kwargs = {
                "model": effective_model,
                "messages": messages,
                "stream": stream,
            }
            p = params if params is not None else self.params
            if p is not None:
                kwargs.update(p.to_kwargs())
            if tools:
                kwargs["tools"] = tools

            response = self._client.chat.completions.create(**kwargs)

            if stream:
                return _StreamHandler(response, self, remember), None

            msg = response.choices[0].message

            if msg.tool_calls:
                assistant_msg = {
                    "role": "assistant",
                    "content": msg.content,
                    "tool_calls": [
                        {
                            "id": tc.id,
                            "type": "function",
                            "function": {
                                "name": tc.function.name,
                                "arguments": tc.function.arguments,
                            },
                        }
                        for tc in msg.tool_calls
                    ],
                }
                if remember:
                    self.history.append(assistant_msg)

                calls = []
                for tc in msg.tool_calls:
                    entry = {"id": tc.id, "name": tc.function.name}
                    try:
                        entry["args"] = json.loads(tc.function.arguments)
                    except (json.JSONDecodeError, TypeError):
                        entry["args"] = {}
                        entry["args_raw"] = tc.function.arguments
                    calls.append(entry)
                return calls, None

            text = msg.content or ""
            if remember:
                self.history.append({"role": "assistant", "content": text})
            return text, None

        except Exception as e:
            return None, e


def ask(url, prompt, **kw):
    """一次性問答，不保留歷史：建立一個用完即丟的 LLM，呼叫 .ask(remember=False)。"""
    llm_kwargs = {"url": url}
    for k in ("model", "system", "key"):
        if k in kw and kw[k] is not None:
            llm_kwargs[k] = kw[k]

    llm = LLM(**llm_kwargs)
    return llm.ask(
        prompt=prompt,
        stream=kw.get("stream", False),
        images=kw.get("images"),
        tools=kw.get("tools"),
        params=kw.get("params"),
        remember=False,
    )


if __name__ == "__main__":
    llm = LLM(url="http://localhost:4000", model="ollama-gemma3-1b")
    reply1, err1 = llm.ask("我的名字是小明，請記住。")
    print("reply1:", reply1, "err1:", err1)
    reply2, err2 = llm.ask("我的名字是什麼？")
    print("reply2:", reply2, "err2:", err2)
