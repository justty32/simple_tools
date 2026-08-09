"""smoke.py — 關卡本體。全離線，一個 token 都不花。

入口在 __main__.py。拆開的理由只有一個：一個檔 150 行以內。
"""

import copy
import os
import re

from . import CAP_FIELDS, Card, CardError, cards, caps_for
from . import engine_for, find, params_for
from .store import aliases

HERE = os.path.dirname(os.path.abspath(__file__))
YAML = os.path.join(os.path.dirname(HERE), "llmkit", "proxy", "litellm.yaml")
BASE = {
    "_version": "0.1.0", "id": "t", "weights": "w", "runner": "r",
    "aliases": {"a": "default"},
    "modes": {"default": {"temperature": {"v": 0.5, "src": 1}}},
    "sources": [{"n": 1, "url": "u", "kind": "official", "read": "2026-08-09"}],
}
_score = []


def ok(name, value=""):
    _score.append(True)
    print(f"  ok   {name}: {value}")


def no(name, why):
    _score.append(False)
    print(f"  FAIL {name}: {why}")


def eq(name, got, want):
    ok(name, repr(got)) if got == want else no(name, f"得到 {got!r}，預期 {want!r}")


def rejects(name, **over):
    """壞掉的 card 要當場被丟出來，不是安靜地收下。"""
    raw = copy.deepcopy(BASE)
    raw.update(over)
    try:
        Card(raw)
    except CardError as exc:
        ok(name, str(exc)[:58])
    else:
        no(name, "收下了，應該要擋")


def yaml_aliases():
    """不裝 pyyaml，直接掃 `- model_name:` 那幾行 —— 這份 yaml 沒複雜到需要 parser。"""
    with open(YAML, encoding="utf-8") as handle:
        return sorted(re.findall(r"^\s*-\s*model_name:\s*(\S+)", handle.read(), re.M))


def check_sample():
    card = find("qwen3.5-9b")
    eq("id 和 alias 都查得到同一張", find("lm-qwen3.5-9b") is card, True)
    eq("alias 決定 mode", card.resolve("lm-qwen3.5-9b-nothink"), ("lm-qwen3.5-9b-nothink", "nothink"))
    eq("給 id 就取第一個 alias", card.resolve("qwen3.5-9b")[0], "lm-qwen3.5-9b")
    eq("think 和 nothink 的建議值不同",
       (card.params("think")["temperature"], card.params("nothink")["temperature"]), (1.0, 0.7))
    eq("context 剝得掉出處", card.context, 262144)
    eq("沒有的 mode 用丟的", _raised(lambda: card.params("nope")), True)
    eq("要靠 allowed_openai_params 的參數列得出來",
       card.needs_allowlist("think"), ["min_p", "repetition_penalty", "top_k"])


def check_params():
    params = params_for("lm-qwen3.5-9b")
    kwargs = params.to_kwargs()
    eq("Params 有欄位的直接送", kwargs["temperature"], 1.0)
    body = params.extra["extra_body"]
    eq("沒欄位的包進 extra_body", sorted(body), ["min_p", "repetition_penalty", "top_k"])
    eq("非 OpenAI 參數不能攤在頂層（SDK 會 TypeError）", "top_k" in kwargs, False)
    eq("extra_body 進得了 kwargs", kwargs["extra_body"]["top_k"], 20)
    eq("override 蓋得掉", params_for("lm-qwen3.5-9b", temperature=0.1).temperature, 0.1)
    grown = params_for("lm-qwen3.5-9b", seed=7, top_k=5)
    eq("override 加新的也照欄位分", (grown.seed, grown.extra["extra_body"]["top_k"]), (7, 5))


def check_caps():
    from llms.caps import FIELDS

    card = find("qwen3.5-9b")
    got = caps_for("lm-qwen3.5-9b")
    eq("預設吐出樣本已實打的基本能力",
       {k: got.get(k) for k in ("tools", "vision", "reasoning")},
       {"tools": True, "vision": True, "reasoning": True})
    eq("trust 打錯字用丟的", _raised(lambda: card.caps("nope")), True)
    eq("能力欄位跟 llms.caps 對得上", set(CAP_FIELDS), set(FIELDS))
    fake = Card({**copy.deepcopy(BASE),
                 "claimed": {"tools": {"v": False, "src": 1}, "vision": {"v": True, "src": 1}},
                 "verified": {"tools": {"v": True, "on": "2026-08-09", "how": "打過"}}})
    eq("查來的不會混進預設，打過的蓋得掉它", fake.caps(), {"tools": True})
    eq("要 claimed 得明講", fake.caps("claimed"), {"tools": True, "vision": True})


def check_engine():
    engine = engine_for("qwen3.5-9b")
    eq("model 用的是 alias 不是 card id", engine.model, "lm-qwen3.5-9b")
    eq("參數掛上去了", engine.params.temperature, 1.0)
    eq("能力 override 掛上去了", engine.caps_override.get("vision"), True)
    eq("kwargs 透傳", engine_for("qwen3.5-9b", url="http://x:9/v1").model, "lm-qwen3.5-9b")


def check_bad_cards():
    rejects("裸值被拒收", modes={"default": {"temperature": 0.5}})
    rejects("src 指到不存在的來源",
            modes={"default": {"temperature": {"v": 0.5, "src": 9}}})
    rejects("sources 跳號",
            sources=[{"n": 2, "url": "u", "kind": "official", "read": "2026-08-09"}])
    rejects("來源類別不認得",
            sources=[{"n": 1, "url": "u", "kind": "blog", "read": "2026-08-09"}])
    rejects("alias 指到不存在的 mode", aliases={"a": "think"})
    rejects("_version 對不上", _version="0.2.0")
    rejects("claimed 有不認得的能力", claimed={"speed": {"v": True, "src": 1}})
    rejects("verified 貼成 claimed 的形狀", verified={"tools": {"v": True, "src": 1}})


def check_yaml():
    declared, have = yaml_aliases(), aliases(cards())
    stray = [a for a in have if a not in declared]
    eq("卡上的 alias 都在 litellm.yaml 裡", stray, [])
    todo = [a for a in declared if a not in have]
    print(f"\n進度：{len(declared) - len(todo)}/{len(declared)} 個 alias 有卡")
    if todo:
        print(f"還沒填：{' '.join(todo)}")


def _raised(fn):
    try:
        fn()
    except CardError:
        return True
    return False


STAGES = (check_sample, check_params, check_caps, check_engine,
          check_bad_cards, check_yaml)


def run():
    """跑完所有關卡，回 True 表示全過。"""
    for stage in STAGES:
        print(f"\n[{stage.__name__.removeprefix('check_')}]")
        stage()
    print(f"\n{sum(_score)}/{len(_score)} 過")
    return all(_score)
