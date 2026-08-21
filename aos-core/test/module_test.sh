#!/usr/bin/env bash
# 「編進來的模組」那條路的檢查，用 examples/module-cpp/greet.cpp。
#
# 最重要的一項是**例外不會帶走 daemon**。C++ 模組是從 C 被呼叫的，
# 讓例外飛過那個邊界是 UB，實務上就是整個 daemon 消失。
# include/aos/plugin.hpp 的 aos::guarded<> 負責擋住，這裡負責證明它真的擋住了。
set -u

daemon_binary="${1:?用法：module_test.sh <aos-daemon> <aos>}"
cli_binary="${2:?}"

work_dir="$(mktemp -d)"
export AOS_SOCKET="${work_dir}/aos.sock"
export XDG_CONFIG_HOME="${work_dir}/config"
unset AOS_PLUGINS

failures=0
checks=0

cleanup() {
  if [ -n "${daemon_pid:-}" ]; then
    kill "${daemon_pid}" 2>/dev/null
    wait "${daemon_pid}" 2>/dev/null
  fi
  rm -rf "${work_dir}"
}
trap cleanup EXIT

check() {
  local name="$1" expected="$2" actual="$3"
  checks=$((checks + 1))
  if [ "${expected}" = "${actual}" ]; then
    printf '  ✓ %s\n' "${name}"
  else
    printf '  ✗ %s\n    預期：%s\n    實際：%s\n' "${name}" "${expected}" "${actual}"
    failures=$((failures + 1))
  fi
}

alive() { kill -0 "${daemon_pid}" 2>/dev/null && echo "活著" || echo "死了"; }

"${daemon_binary}" &
daemon_pid=$!
for _ in $(seq 1 100); do
  [ -S "${AOS_SOCKET}" ] && break
  sleep 0.05
done

echo "── 編進來的模組 ──"
check "列在 daemon plugins 裡" "greet(C++)" \
  "$("${cli_binary}" daemon plugins </dev/null | awk '{print $1}')"
check "標示成編進來的" "(編進來的)" \
  "$("${cli_binary}" daemon plugins </dev/null | awk '{print $2}')"
check "命令可以跑" "你好，世界" "$("${cli_binary}" greet 世界 </dev/null)"
check "子命令" "1 2 3" "$("${cli_binary}" greet count 3 </dev/null)"
# C++ 這邊拿到的是跟內建命令一樣的串流介面。
check "串流" "fedcba" "$(printf 'abcdef' | "${cli_binary}" greet reverse)"

echo "── 例外不能穿過 C 邊界 ──"
# std::stoi 丟出來的——「不是你寫的那行在丟」的典型例子。
"${cli_binary}" greet count abc </dev/null >/dev/null 2>&1
check "std::stoi 丟例外時 exit code 是 1" "1" "$?"
check "daemon 沒被帶走" "活著" "$(alive)"

check "例外訊息會送到 stderr" "這是一個故意丟出來的例外，daemon 應該還活著" \
  "$("${cli_binary}" greet boom </dev/null 2>&1 >/dev/null)"
check "daemon 還是活著" "活著" "$(alive)"

# 一次沒事可能只是運氣好，連續丟才看得出是不是真的接住了。
for _ in $(seq 1 20); do
  "${cli_binary}" greet boom </dev/null >/dev/null 2>&1
done
check "丟 20 次之後仍然正常服務" "pong" "$("${cli_binary}" ping </dev/null)"

echo "── 內建命令沒有被影響 ──"
check "echo 還在" "hi" "$(printf 'hi' | "${cli_binary}" echo)"

echo
if [ "${failures}" -gt 0 ]; then
  printf '%d／%d 項檢查失敗\n' "${failures}" "${checks}"
  exit 1
fi
printf '%d 項檢查全過\n' "${checks}"
