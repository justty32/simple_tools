#!/usr/bin/env bash
# 啟動 litellm proxy，預設吃同目錄的 litellm.yaml
CONFIG="${1:-$(dirname "$0")/litellm.yaml}"
PORT="${2:-4000}"
exec litellm --config "$CONFIG" --port "$PORT"
