"""discover.py — 從環境變數 `FREEPY_TOOLS` 掃出工具，讀出它們的 spec。

> **還沒成型，長在地基上面那一層。** 格式和標準庫已經定型在 `../llmkit/tooljson/`，
> 那邊沒有隱含的搜尋路徑：要讀哪幾份 .json 是呼叫端明講的。這個檔是「自動掃一個
> 資料夾」那一層的草稿，定型了再搬進 llmkit。同一份 PLAN.md 裡的 `describe.py`
> （讓 LLM 讀腳本產 spec）也還沒開始。

    export FREEPY_TOOLS=~/tools:~/work/bin        # os.pathsep 分隔，比照 PATH

**只認 FREEPY_TOOLS，不碰 PATH。**PATH 上有幾百個東西，全部產 spec 是災難，而且模型
的 context 塞不下。要成為 tool 就得被明確放進來，這個門檻是故意的 —— 一般指令走
base_tools 的 run_shell 就好，模型早就會用 ls / grep 了。

spec 放在工具旁邊的 `.specs/*.json`，工具跟它的描述一起搬、一起進 git，一個資料夾
自我完備。**掃的是整個 `.specs/` 而不是「跟執行檔同名的那份」**，因為一個 .json 可以
裝好幾個 tool，也可以描述別的檔案 —— 檔名跟內容沒有硬綁定。

「哪些執行檔還沒有 spec」是反推出來的：把所有 spec 指到的執行檔收集起來，剩下的就是
`missing`。**沒有 spec 的可執行檔不會變成 tool**，因為產 spec 要花 LLM，那是
describe.py 的事，不是掃描的事。
"""

import os

from tooljson import SpecError, load_all

ENV = "FREEPY_TOOLS"
SPEC_DIR = ".specs"


def roots():
    """FREEPY_TOOLS 拆成一串存在的資料夾，順序照寫的順序，重複的只留第一次。"""
    raw = os.environ.get(ENV) or ""
    out = []
    for piece in raw.split(os.pathsep):
        piece = piece.strip()
        if not piece:
            continue
        path = os.path.abspath(os.path.expanduser(piece))
        if path not in out and os.path.isdir(path):
            out.append(path)
    return out


def _listdir(directory):
    try:
        return sorted(os.listdir(directory))
    except OSError:
        return []


def executables(directory):
    """一個資料夾裡的可執行檔，排序後回傳絕對路徑。點開頭的和資料夾都跳過。"""
    out = []
    for name in _listdir(directory):
        path = os.path.join(directory, name)
        if not name.startswith(".") and os.path.isfile(path) and os.access(path, os.X_OK):
            out.append(path)
    return out


def spec_files(directory):
    """一個工具資料夾底下 `.specs/` 裡的所有 .json，排序後回傳絕對路徑。"""
    holder = os.path.join(directory, SPEC_DIR)
    return [os.path.join(holder, n) for n in _listdir(holder) if n.endswith(".json")]


def spec_path(exec_path):
    """新產的單一 spec 該放哪：同目錄下的 `.specs/<檔名>.json`。只是慣例，不是規則。"""
    directory, name = os.path.split(os.path.abspath(exec_path))
    return os.path.join(directory, SPEC_DIR, name + ".json")


def scan():
    """掃過所有 root，回 (specs, missing, errors)。

    specs 是 name -> Spec，**同名的以先出現的為準**，比照 PATH 的規矩。
    missing 是還沒有任何 spec 指到的可執行檔，errors 是壞掉的 (spec 檔, 訊息)。
    後兩個都不會讓掃描中斷 —— 一個壞檔不該讓其他九個工具消失。
    """
    specs, errors, covered = {}, [], set()
    where = roots()
    for root in where:
        for path in spec_files(root):
            try:
                found = load_all(path)
            except SpecError as e:
                errors.append((path, str(e)))
                continue
            for one in found:
                specs.setdefault(one.name, one)
                covered.add(one.body.target)
    missing = [p for root in where for p in executables(root) if p not in covered]
    return specs, missing, errors
