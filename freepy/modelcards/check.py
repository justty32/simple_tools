"""check.py — card 的規範常數，和照著它驗證一份 raw dict。

規範本身寫在 FORMAT.md，這個檔是它可執行的那一半。驗證是**用丟的**
（`CardError`），不是回傳字串 —— 壞掉的 card 是人打錯字，越早炸越好，
跟工具的回傳值不一樣（那個要回字串給模型看）。
"""

VERSION = "0.1.0"

# 對齊 llms/caps.py 的 FIELDS，這樣 verified 可以直接生成 litellm.yaml 的 model_info
CAP_FIELDS = ("tools", "tool_choice", "parallel_tools", "vision",
              "reasoning", "json_schema", "caching")

# llms.Params 有欄位的參數。不在這裡面的一律進 Params.extra，而 extra 裡的東西
# 可能被 proxy 的 drop_params 無聲吞掉 —— 見 Card.needs_allowlist()。
NATIVE_PARAMS = ("temperature", "top_p", "max_tokens", "seed", "stop",
                 "presence_penalty", "frequency_penalty")

SOURCE_KINDS = ("official", "runner", "secondary")
REQUIRED = ("_version", "id", "weights", "runner", "aliases", "modes", "sources")


class CardError(Exception):
    """card 的格式壞掉。"""


def check(raw, where):
    """驗一份 raw dict，不合格就丟 CardError。where 只是訊息前綴。"""

    def bad(msg):
        raise CardError(f"{where}: {msg}")

    if not isinstance(raw, dict):
        bad("最外層要是 object")
    for key in REQUIRED:
        if key not in raw:
            bad(f"缺 {key}")
    if raw["_version"] != VERSION:
        bad(f"_version 是 {raw['_version']!r}，這份實作只認得 {VERSION!r}")
    for key in ("id", "weights", "runner"):
        if not isinstance(raw[key], str) or not raw[key]:
            bad(f"{key} 要是非空字串")
    nums = _sources(raw["sources"], bad)
    _modes(raw["modes"], nums, bad)
    if "context" in raw:
        _value("context", raw["context"], nums, bad)
    for alias, mode in raw["aliases"].items():
        if mode not in raw["modes"]:
            bad(f"alias {alias!r} 指到不存在的 mode {mode!r}")
    for name, node in raw.get("claimed", {}).items():
        _cap_name("claimed", name, bad)
        _value(f"claimed.{name}", node, nums, bad)
    for name, node in raw.get("verified", {}).items():
        _cap_name("verified", name, bad)
        if not isinstance(node, dict) or set(node) != {"v", "on", "how"}:
            bad(f"verified.{name} 要剛好是 {{'v', 'on', 'how'}} —— 打過的沒有 src，"
                "來源就是我們自己")
        if not isinstance(node["v"], bool):
            bad(f"verified.{name}.v 要是 true / false")


def _cap_name(holder, name, bad):
    if name not in CAP_FIELDS:
        bad(f"{holder} 有不認得的能力 {name!r}，只能是 {list(CAP_FIELDS)}")


def _sources(sources, bad):
    if not isinstance(sources, list) or not sources:
        bad("sources 要是非空 list —— 沒有出處的卡沒有價值")
    nums = []
    for i, src in enumerate(sources, start=1):
        if not isinstance(src, dict) or set(src) != {"n", "url", "kind", "read"}:
            bad(f"sources[{i - 1}] 要剛好有 n / url / kind / read 四個鍵")
        if src["n"] != i:
            bad(f"sources 的 n 要從 1 開始不跳號，第 {i} 筆是 {src['n']!r}")
        if src["kind"] not in SOURCE_KINDS:
            bad(f"sources[{i - 1}].kind 是 {src['kind']!r}，只能是 {list(SOURCE_KINDS)}")
        nums.append(i)
    return nums


def _modes(modes, nums, bad):
    if not isinstance(modes, dict) or not modes:
        bad("modes 要是非空 object")
    for mode, table in modes.items():
        if not isinstance(table, dict):
            bad(f"modes.{mode} 要是 object")
        for name, node in table.items():
            _value(f"modes.{mode}.{name}", node, nums, bad)


def _value(label, node, nums, bad):
    """值節點一律是 {"v": 值, "src": 編號}。出處是這張表唯一的價值，裸值等於沒有。"""
    if not isinstance(node, dict) or "v" not in node:
        bad(f"{label} 要是 {{'v': 值, 'src': 編號}}，不能是裸值")
    if node.get("src") not in nums:
        bad(f"{label} 的 src={node.get('src')!r} 不在 sources 裡（有的是 {nums}）")
