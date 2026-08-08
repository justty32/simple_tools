#!/usr/bin/env bash
# 啟動 litellm proxy，預設吃同目錄的 litellm.yaml。
#
# 用 uv run 臨時把 litellm 拉進來跑，不用先裝、也不用維護 venv。
#
# fastapi 版本要夾在 [0.119, 0.130) 之間：
# - >=0.130 附近某版拿掉了 litellm[proxy] 內部依賴的私有 API
#   (fastapi.dependencies.utils.get_flat_dependant)，proxy 開機就 crash。
# - <0.119 會讓 uv 把 litellm 反向解析回 1.79.x 這種老版本，那個版本的
#   Ollama function calling 是用文字把工具定義塞進 system prompt 裡叫模型
#   自己接龍 JSON，不是走 /api/chat 原生 tools，多工具情境下很容易垮掉
#   （2026-08-06 實測：3 個工具以上就常常漏答成純文字，不會進 tool_calls）。
# 2026-08-06 實測 fastapi<0.130 落在 litellm 1.87.5，開機正常、原生 tool
# calling 也正常，暫時定案用這個上限。之後升版遇到新的相容性問題再調整。
set -euo pipefail
CONFIG="${1:-$(dirname "$0")/litellm.yaml}"
PORT="${2:-4000}"
exec uv run --with 'litellm[proxy]' --with 'fastapi<0.130' \
    litellm --config "$CONFIG" --port "$PORT"
