"""params.py — 一次 API 呼叫要帶的參數。

只做一件事：把有設定的欄位整理成 kwargs，沒設定的（None）一律不送，
讓 proxy / 模型自己用預設值。
"""

from dataclasses import dataclass, field


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
