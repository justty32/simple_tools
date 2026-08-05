#!/usr/bin/env bash
# 啟動 litellm proxy，預設吃同目錄的 litellm.yaml。
#
# 用 uv run 臨時把 litellm 拉進來跑，不用先裝、也不用維護 venv。
# fastapi 釘在 0.119 以下：新版跟 litellm[proxy] 目前的 pydantic 相容性會出事。
set -euo pipefail
CONFIG="${1:-$(dirname "$0")/litellm.yaml}"
PORT="${2:-4000}"
exec uv run --with 'litellm[proxy]' --with 'fastapi<0.119' \
    litellm --config "$CONFIG" --port "$PORT"
