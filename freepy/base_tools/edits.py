"""edits.py — 改檔案裡的一段文字。

只做「一字不差的字串取代」，不做 diff、不做 patch、不做正則。原因很單純：
模型產生的 unified diff 常常行號對不上，而「把這段換成那段」它幾乎不會弄錯。

預設 old 在檔案裡只能出現一次，出現多次就要求呼叫端明講 replace_all —— 不然模型
以為改了一處，其實動到三處，而且它不會知道。
"""

from .paths import resolve, show


def edit_file(path: str, old: str, new: str, replace_all: bool = False) -> str:
    """把檔案裡的一段文字換成另一段。old 必須一字不差，包含縮排。

    Args:
        path: 檔案路徑，相對路徑會接在工作根目錄底下
        old: 要被換掉的原文，預設必須在檔案中剛好出現一次
        new: 要換成的新內容，給空字串就是刪掉
        replace_all: 設成 true 才允許一次換掉全部出現的地方
    """
    full, err = resolve(path)
    if err:
        return err

    if not isinstance(old, str) or not old:
        return "Error: old must be a non-empty string"
    new = new if isinstance(new, str) else str(new)
    if old == new:
        return "Error: old and new are identical, nothing to do"

    if full.is_dir():
        return f"Error: {show(full)} is a directory, not a file"
    try:
        text = full.read_text(encoding="utf-8")
    except FileNotFoundError:
        return f"Error: file not found: {show(full)}"
    except UnicodeDecodeError:
        return f"Error: {show(full)} is not a utf-8 text file"
    except Exception as e:
        return f"Error: cannot read {show(full)}: {e}"

    hits = text.count(old)
    if hits == 0:
        return (f"Error: that text does not appear in {show(full)}. "
                "Read the file again and copy the exact text, whitespace included.")
    if hits > 1 and not replace_all:
        return (f"Error: that text appears {hits} times in {show(full)}. "
                "Include more surrounding lines to make it unique, or pass replace_all=true.")

    updated = text.replace(old, new) if replace_all else text.replace(old, new, 1)
    try:
        full.write_text(updated, encoding="utf-8")
    except Exception as e:
        return f"Error: cannot write {show(full)}: {e}"

    changed = hits if replace_all else 1
    return f"Replaced {changed} occurrence(s) in {show(full)}"
