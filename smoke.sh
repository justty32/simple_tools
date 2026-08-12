#!/usr/bin/env bash
# One-command health check for the two runnable projects in this repository.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVE=0

usage() {
    cat <<'EOF'
Usage: ./smoke.sh [--live]

Without options, run every offline check plus the dcap C/C++ build probes.
With --live, also require DEEPSEEK_API_KEY and LM Studio, start a temporary
LiteLLM proxy when port 4000 is free, and exercise every configured home model.
EOF
}

case "${1:-}" in
    "") ;;
    --live) LIVE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac
if (($# > 1)); then
    usage >&2
    exit 2
fi

step() {
    printf '\n==> %s\n' "$1"
    shift
    "$@"
}

has_http() {
    curl --silent --show-error --fail --max-time 2 "$1" >/dev/null 2>&1
}

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if [[ -n "${PROXY_PID:-}" ]]; then
        kill "$PROXY_PID" 2>/dev/null || true
        wait "$PROXY_PID" 2>/dev/null || true
    fi
    if [[ -n "${PROBE_DIR:-}" && "$PROBE_DIR" == /tmp/simple-tools-smoke.* ]]; then
        rm -rf -- "$PROBE_DIR"
    fi
    exit "$status"
}
trap cleanup EXIT INT TERM

if ((LIVE == 1)); then
    [[ -n "${DEEPSEEK_API_KEY:-}" ]] || {
        printf '\nERROR: DEEPSEEK_API_KEY is not set\n' >&2
        exit 1
    }
    has_http http://127.0.0.1:1234/v1/models || {
        printf '\nERROR: LM Studio is not serving http://127.0.0.1:1234/v1\n' >&2
        exit 1
    }
fi

cd "$ROOT/freepy"
step "base_tools" env PYTHONPATH=llmkit uv run python -m base_tools
step "exec_tools" env PYTHONPATH=llmkit uv run python -m exec_tools
step "http_tools" env PYTHONPATH=llmkit uv run python -m http_tools
step "agentloop" env PYTHONPATH=llmkit uv run python -m agentloop
step "Pi bridge protocol" uv run python adapters/pi/check_pi_bridge.py
step "shell helpers" env PYTHONPATH=llmkit uv run python -m shells._checks
step "Pi launcher" env PYTHONPATH=llmkit uv run python -m shells._checks_pi

cd "$ROOT/freepy/llmkit"
step "tooljson" uv run python -m tooljson
step "tooljson spec" uv run python -m tooljson._checks_spec
step "tooljson Python adapter" uv run python -m tooljson._checks_python
step "llms Reply" uv run python -m llms._checks_reply

cd "$ROOT"
if command -v pi >/dev/null 2>&1; then
    step "Pi executable" pi --version
else
    printf '\nERROR: pi is not on PATH\n' >&2
    exit 1
fi
if command -v claude >/dev/null 2>&1; then
    step "Claude Code executable" claude --version
else
    printf '\nERROR: claude is not on PATH\n' >&2
    exit 1
fi

step "build dcap" cmake -S "$ROOT/dcap" -B "$ROOT/dcap/build"
step "compile dcap" cmake --build "$ROOT/dcap/build"

PROBE_DIR="$(mktemp -d /tmp/simple-tools-smoke.XXXXXX)"
cd "$PROBE_DIR"
step "generate C++ project" "$ROOT/dcap/bin/dcap" cpp smoke_cpp
step "build C++ project" cmake -S smoke_cpp -B smoke_cpp/build
step "compile C++ project" cmake --build smoke_cpp/build
CPP_OUTPUT="$(smoke_cpp/bin/smoke_cpp)"
[[ "$CPP_OUTPUT" == "2 + 3 = 5" ]] || {
    printf 'ERROR: unexpected C++ output: %s\n' "$CPP_OUTPUT" >&2
    exit 1
}
step "generate C project" "$ROOT/dcap/bin/dcap" c smoke_c
step "build C project" cmake -S smoke_c -B smoke_c/build
step "compile C project" cmake --build smoke_c/build
C_OUTPUT="$(smoke_c/bin/smoke_c)"
[[ "$C_OUTPUT" == "2 + 3 = 5" ]] || {
    printf 'ERROR: unexpected C output: %s\n' "$C_OUTPUT" >&2
    exit 1
}
printf '\nPASS: offline checks and dcap probes\n'

if ((LIVE == 0)); then
    exit 0
fi

# Recheck after the offline suite: it can take long enough for a local server to
# have been closed in the meantime.
has_http http://127.0.0.1:1234/v1/models || {
    printf '\nERROR: LM Studio is not serving http://127.0.0.1:1234/v1\n' >&2
    exit 1
}

if has_http http://127.0.0.1:4000/v1/models; then
    printf '\n==> use existing LiteLLM proxy on port 4000\n'
else
    printf '\n==> start temporary LiteLLM proxy on port 4000\n'
    PROXY_LOG="$PROBE_DIR/litellm.log"
    "$ROOT/freepy/llmkit/proxy/start_litellm.sh" \
        "$ROOT/freepy/llmkit/proxy/litellm.yaml" 4000 >"$PROXY_LOG" 2>&1 &
    PROXY_PID=$!
    for _ in {1..180}; do
        if has_http http://127.0.0.1:4000/v1/models; then
            break
        fi
        if ! kill -0 "$PROXY_PID" 2>/dev/null; then
            printf 'ERROR: LiteLLM proxy exited; log follows\n' >&2
            tail -80 "$PROXY_LOG" >&2
            exit 1
        fi
        sleep 1
    done
    has_http http://127.0.0.1:4000/v1/models || {
        printf 'ERROR: LiteLLM proxy did not become ready; log follows\n' >&2
        tail -80 "$PROXY_LOG" >&2
        exit 1
    }
fi

cd "$ROOT/freepy/llmkit"
step "DeepSeek and LM Studio live models" uv run python live_smoke.py
printf '\nPASS: complete live smoke test\n'
