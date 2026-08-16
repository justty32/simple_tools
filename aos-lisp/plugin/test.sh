#!/usr/bin/env bash
# 真的起一個 aos-daemon、把 aos-lisp.so 掛上去、用真的 CLI 打它。
#
# 這是唯一能證明整條路走得通的測試：CLI → UDS → 連線執行緒 → loop 的 queue
# → loop 執行緒的 Janet VM → 回來。單元測試看不到那幾次跨執行緒交接。
set -u

here="$(cd "$(dirname "$0")" && pwd)"
aos_core="${AOS_CORE:-${here}/../../aos-core}"
daemon_binary="${aos_core}/bin/aos-daemon"
cli="${aos_core}/bin/aos"
plugin="${here}/build/aos-lisp.so"

for f in "${daemon_binary}" "${cli}" "${plugin}"; do
  [ -x "$f" ] || { echo "缺少 $f（先 make）" >&2; exit 1; }
done

work_dir="$(mktemp -d)"
export AOS_SOCKET="${work_dir}/aos.sock"
export AOS_PLUGINS="${plugin}"
export XDG_CONFIG_HOME="${work_dir}/config"   # 別讓使用者自己的外掛影響結果

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

contains() {
  local name="$1" needle="$2" haystack="$3"
  checks=$((checks + 1))
  if printf '%s' "${haystack}" | grep -qF -- "${needle}"; then
    printf '  ✓ %s\n' "${name}"
  else
    printf '  ✗ %s\n    找不到：%s\n    實際：%s\n' "${name}" "${needle}" "${haystack}"
    failures=$((failures + 1))
  fi
}

"${daemon_binary}" &
daemon_pid=$!
for _ in $(seq 1 100); do
  [ -S "${AOS_SOCKET}" ] && break
  sleep 0.05
done
[ -S "${AOS_SOCKET}" ] || { echo "daemon 沒有起來" >&2; exit 1; }

echo "── 外掛載進來了 ──"
contains "外掛在清單裡" "aos-lisp" "$("${cli}" daemon plugins </dev/null 2>&1)"
contains "命令進了命令樹" "lisp" "$("${cli}" help </dev/null 2>&1)"
contains "lisp 是分組節點" "eval" "$("${cli}" lisp </dev/null 2>&1)"

echo "── 求值 ──"
check "算術" "3" "$("${cli}" lisp eval a '(+ 1 2)' </dev/null)"
check "exit status 0" "0" "$("${cli}" lisp eval a '(+ 1 2)' </dev/null >/dev/null 2>&1; echo $?)"
check "多個 form，取最後一個的值" "42" "$("${cli}" lisp eval a '(def n 6) (* n 7)' </dev/null)"
check "字串原樣輸出（不是跳脫位元組）" "中文" "$("${cli}" lisp eval a '"中文"' </dev/null)"
check "從 stdin 讀" "7" "$(echo '(+ 3 4)' | "${cli}" lisp eval a)"

echo "── ★ 一條 loop 一張長存 env ──"
"${cli}" lisp eval alpha '(def x 7)' </dev/null >/dev/null
check "同一條 loop 看得到上次定義的" "7" "$("${cli}" lisp eval alpha 'x' </dev/null)"
check "另一條 loop 看不到" "1" "$("${cli}" lisp eval beta 'x' </dev/null >/dev/null 2>&1; echo $?)"
contains "而且說得出原因" "unknown symbol x" "$("${cli}" lisp eval beta 'x' </dev/null 2>&1)"
"${cli}" lisp eval beta '(def x :beta-的)' </dev/null >/dev/null
check "兩條 loop 各自的值互不干擾" "7" "$("${cli}" lisp eval alpha 'x' </dev/null)"
check "beta 是自己的" ":beta-的" "$("${cli}" lisp eval beta 'x' </dev/null)"

echo "── 錯誤不會炸掉 loop ──"
check "錯誤的 exit status 是 1" "1" "$("${cli}" lisp eval a '(error :boom)' </dev/null >/dev/null 2>&1; echo $?)"
contains "有堆疊" "error: boom" "$("${cli}" lisp eval a '(error :boom)' </dev/null 2>&1)"
contains "堆疊指得出位置" "in thunk" "$("${cli}" lisp eval a '(error :boom)' </dev/null 2>&1)"
contains "壞語法被抓到" "unexpected end of source" "$("${cli}" lisp eval a '(+ 1' </dev/null 2>&1)"
check "壞語法的 exit status 是 1" "1" "$("${cli}" lisp eval a '(+ 1' </dev/null >/dev/null 2>&1; echo $?)"
check "★ 出過事之後同一條 loop 照跑" "5" "$("${cli}" lisp eval a '(+ 2 3)' </dev/null)"
check "★ 而且之前定義的東西還在" "42" "$("${cli}" lisp eval a '(* n 7)' </dev/null)"

echo "── print 導向呼叫端 ──"
check "print 出去的東西收得到" "$(printf 'hi\nnil')" "$("${cli}" lisp eval a '(print "hi")' </dev/null)"

echo "── loop 管理 ──"
contains "列得出 alpha" "alpha" "$("${cli}" lisp loops </dev/null 2>&1)"
contains "列得出 beta" "beta" "$("${cli}" lisp loops </dev/null 2>&1)"
contains "stop 有回應" "收工" "$("${cli}" lisp stop beta </dev/null 2>&1)"
check "stop 過的不在清單裡" "" "$("${cli}" lisp loops </dev/null 2>&1 | grep -c '^beta ' | grep -v '^0$')"
check "stop 不存在的回 1" "1" "$("${cli}" lisp stop no-such </dev/null >/dev/null 2>&1; echo $?)"
check "★ stop 之後同名再用會是全新的 env" "1" "$("${cli}" lisp eval beta 'x' </dev/null >/dev/null 2>&1; echo $?)"

echo "── 併發：多條連線同時打 ──"
# ⚠ 不能用光禿禿的 `wait`：那會連背景的 daemon 一起等，測試就永遠不會結束。
#   只等這 8 個。
pids=()
for i in 1 2 3 4 5 6 7 8; do
  ( "${cli}" lisp eval "worker-${i}" "(* ${i} 100)" </dev/null > "${work_dir}/out-${i}" ) &
  pids+=("$!")
done
wait "${pids[@]}"
concurrent_ok=1
for i in 1 2 3 4 5 6 7 8; do
  [ "$(cat "${work_dir}/out-${i}")" = "$((i * 100))" ] || concurrent_ok=0
done
check "8 條 loop 同時開、結果都對" "1" "${concurrent_ok}"

echo "── ★★ unix/call：C++ 分得出 exit 137 和 SIGKILL ──"
mk() { mkdir -p "${work_dir}/$1"; : > "${work_dir}/$1/stdin"; }
call() {
  local d="$1"; shift
  "${cli}" lisp eval u "(unix/call :argv [$*] :cwd \"${work_dir}\" \
     :stdin \"${work_dir}/${d}/stdin\" :stdout \"${work_dir}/${d}/stdout\" \
     :stderr \"${work_dir}/${d}/stderr\" :status \"${work_dir}/${d}/status\")" </dev/null 2>&1
}

mk ok
contains "正常退出是 :exited" ':status :exited' "$(call ok '"/bin/sh" "-c" "exit 0"')"
mk nz
contains "nonzero 也是 :exited" ':code 7' "$(call nz '"/bin/sh" "-c" "exit 7"')"

mk e137
out137="$(call e137 '"/bin/sh" "-c" "exit 137"')"
mk k9
# ⚠ 這裡不能寫 \$\$：$* 展開的結果不會再被 shell 處理跳脫，所以反斜線會原樣
#   進到 Janet 字串裡，Janet 就報 invalid string escape sequence。單引號已經
#   擋住 shell 展開了，直接寫 $$。
outk9="$(call k9 '"/bin/sh" "-c" "kill -9 $$"')"
contains "★ exit 137 是確定的 :exited" ':status :exited' "${out137}"
contains "★ 而且 code 是 137" ':code 137' "${out137}"
contains "★ SIGKILL 是 :signalled，不是 exit 137" ':status :signalled' "${outk9}"
contains "★ 而且說得出是 9 號信號" ':signal 9' "${outk9}"

mk noexe
contains "★ 執行檔不存在是 :launch-error" ':status :launch-error' "$(call noexe '"/no/such/binary-xyz"')"
mk e127
contains "★ 而「程式回 127」是 :exited，兩者分得開" ':code 127' "$(call e127 '"/bin/sh" "-c" "exit 127"')"

echo "── unix/call 的證據檔 ──"
mk io
printf 'fed in\n' > "${work_dir}/io/stdin"
call io '"/bin/sh" "-c" "cat; echo err >&2"' >/dev/null
check "stdin 餵進去了" "fed in" "$(cat "${work_dir}/io/stdout")"
check "stderr 落到指定的檔" "err" "$(cat "${work_dir}/io/stderr")"
contains "status 檔是可以 parse 的 Janet" ':status :exited' "$(cat "${work_dir}/io/status")"
check "原子寫入的 .tmp 沒留下" "" "$(ls "${work_dir}/io/" | grep '\.tmp$')"
check "read-status 讀得回來" ":exited" \
  "$("${cli}" lisp eval u "(get (unix/read-status \"${work_dir}/io/status\") :status)" </dev/null)"
check "★ 檔案不存在回 nil = 還不知道" "nil" \
  "$("${cli}" lisp eval u '(unix/read-status "/no/such/status")' </dev/null)"

mk cwdtest
call cwdtest '"/bin/pwd"' >/dev/null
check "cwd 生效" "${work_dir}" "$(cat "${work_dir}/cwdtest/stdout")"

mk dirty
printf 'old evidence\n' > "${work_dir}/dirty/status"
check "★ 不覆蓋既有證據" "1" "$(call dirty '"/bin/echo" "x"' >/dev/null 2>&1; echo $?)"
check "而且真的沒動它" "old evidence" "$(cat "${work_dir}/dirty/status")"

mk relative
contains "★ argv[0] 相對路徑被拒絕" "絕對路徑" "$(call relative '"echo" "x"')"

echo "── 收工 ──"
"${cli}" daemon stop </dev/null >/dev/null 2>&1
for _ in $(seq 1 100); do
  kill -0 "${daemon_pid}" 2>/dev/null || break
  sleep 0.05
done
if kill -0 "${daemon_pid}" 2>/dev/null; then
  check "★ daemon 收工（loop 執行緒有被 join）" "已結束" "還活著"
else
  check "★ daemon 收工（loop 執行緒有被 join）" "已結束" "已結束"
  daemon_pid=""
fi

echo
if [ "${failures}" -eq 0 ]; then
  printf '%d 項檢查全過\n' "${checks}"
  exit 0
fi
printf '%d / %d 項失敗\n' "${failures}" "${checks}"
exit 1
