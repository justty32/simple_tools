"""docstrings.py — 從 docstring 挖出函式描述和每個參數的說明。

挖不到就回空的，不猜也不抱怨；schema 少一句 description 又不會死。
"""

import re


def summary(doc):
    """取 docstring 第一行非空白內容，當作函式的簡短描述。"""
    if not doc:
        return ""
    for line in doc.strip().splitlines():
        line = line.strip()
        if line:
            return line
    return ""


def param_descriptions(doc):
    """從 docstring 解析每個參數的說明文字，依序嘗試三種常見格式。"""
    if not doc:
        return {}

    # 1. Google style 的 Args: 區塊，例如 "name: description"
    m = re.search(r"Args:\s*\n(.*?)(?:\n\s*\n|\Z)", doc, re.S)
    if m:
        found = {}
        for line in m.group(1).splitlines():
            pm = re.match(r"\s*(\w+)\s*(?:\([^)]*\))?\s*:\s*(.+)", line)
            if pm:
                found[pm.group(1)] = pm.group(2).strip()
        if found:
            return found

    # 2. Sphinx style 的 :param name: description
    sphinx = re.findall(r":param\s+(\w+)\s*:\s*(.+)", doc)
    if sphinx:
        return {name: desc.strip() for name, desc in sphinx}

    # 3. 最寬鬆的 fallback：文件字串裡任何一行 "name: description"
    simple = {}
    for line in doc.splitlines():
        pm = re.match(r"\s*(\w+)\s*:\s*(.+)", line)
        if pm:
            simple[pm.group(1)] = pm.group(2).strip()
    return simple
