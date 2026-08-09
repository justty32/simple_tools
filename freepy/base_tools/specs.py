"""specs.py — 把這四個工具宣告成 tooljson 的 spec，存成一份 .json。

    cd freepy && PYTHONPATH=llmkit uv run python -m base_tools.specs   # 重產 tools.json

**這一步只跑一次**，改了下面的宣告才要再跑。產出的 `tools.json` 讀回來跟
`base_tools.tools()` 是同一組能力：

    schemas, dispatch = tooljson.tools("base_tools/tools.json")   # 設定檔
    schemas, dispatch = base_tools.tools()                        # import

差別不在能力，在**能力從哪裡來**。`tools()` 是「這支程式 import 了 base_tools」，
.json 是「這個 agent 的設定檔裡列了這四個」—— 後者可以跟別的 .json（exec 型的
外部執行檔）混在一起，可以在不改程式的情況下砍掉 `run_shell`，也可以把描述改成
別的語氣去試模型的反應。用不到這三件事就用 `tools()`，不必繞這一圈。

**schema 是手寫的，不是從簽名反射出來的**（理由見 tooljson 的 PYTHON.md）。
代價是它會跟 `files.py` / `edits.py` / `shell.py` 的簽名走散 —— `Spec.stale` 只看得到
這個檔的指紋，看不到那三個檔。所以改那三個檔的簽名時，這裡要自己跟上。

`set_root()` 照樣有效而且照樣要設：.json 那條路只是換一種方式 import 進同一個
行程，工作根目錄還是 `paths.py` 裡那一個全域的。
"""

import tooljson

from . import edits, files, shell

PATH = "tools.json"  # 存在 base_tools/ 底下，跟這個檔同一層

_PATH_ARG = {"type": "string", "description": "檔案路徑，相對路徑會接在工作根目錄底下"}


class ReadFile(tooljson.Tool):
    name = "read_file"
    description = "讀一個文字檔，回傳的每一行前面都附上行號。"
    params = {
        "path": _PATH_ARG,
        "offset": {"type": "integer", "description": "從第幾行開始讀，從 1 算起"},
        "limit": {"type": "integer", "description": "最多讀幾行，檔案很大時分次讀"},
    }
    required = ["path"]

    def run(self, path, offset=1, limit=2000) -> str:
        return files.read_file(path, offset, limit)


class WriteFile(tooljson.Tool):
    name = "write_file"
    description = "把文字寫進檔案。檔案已經存在就整個覆寫，父資料夾不存在會自動建立。"
    params = {
        "path": _PATH_ARG,
        "content": {"type": "string", "description": "要寫進去的完整內容"},
    }
    required = ["path", "content"]

    def run(self, path, content) -> str:
        return files.write_file(path, content)


class EditFile(tooljson.Tool):
    name = "edit_file"
    description = "把檔案裡的一段文字換成另一段。old 必須一字不差，包含縮排。"
    params = {
        "path": _PATH_ARG,
        "old": {"type": "string", "description": "要被換掉的原文，預設必須在檔案中剛好出現一次"},
        "new": {"type": "string", "description": "要換成的新內容，給空字串就是刪掉"},
        "replace_all": {"type": "boolean",
                        "description": "設成 true 才允許一次換掉全部出現的地方"},
    }
    required = ["path", "old", "new"]

    def run(self, path, old, new, replace_all=False) -> str:
        return edits.edit_file(path, old, new, replace_all)


class RunShell(tooljson.Tool):
    name = "run_shell"
    description = "在工作根目錄底下執行一行 shell 指令，回傳它的輸出和結束碼。"
    params = {
        "command": {"type": "string", "description": "要執行的指令，就是你會在終端機打的那一行"},
        "timeout": {"type": "integer", "description": "最多等幾秒，逾時會殺掉並回傳已經印出來的部分"},
    }
    required = ["command"]

    def run(self, command, timeout=60) -> str:
        return shell.run_shell(command, timeout)


#: 順序就是給模型看的順序，跟 `base_tools.ALL` 一致
ALL = (ReadFile, WriteFile, EditFile, RunShell)


def build(path=None) -> str:
    """把四份 spec 寫成一個 .json（一個檔可以裝好幾個 tool），回它的路徑。

    `path` 給 `".."`：讀取端會把 `freepy/` 插進 `sys.path` 再 `import base_tools.specs`，
    所以這份 .json 自我完備，從哪個目錄讀都不會散。
    """
    import os
    where = path or os.path.join(os.path.dirname(os.path.abspath(__file__)), PATH)
    tooljson.save([tooljson.from_tool(one, path="..") for one in ALL], where)
    return where


if __name__ == "__main__":
    print("寫出", build())
