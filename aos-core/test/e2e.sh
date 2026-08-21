#!/usr/bin/env bash
# 真的起一個 daemon，用真的 CLI 打它。
#
# 這是唯一能證明「串流端到端可用」的測試——單元測試看不到 CLI 與 daemon 之間
# 那條真的 socket，而串流的坑幾乎都在那裡。
set -u

daemon_binary="${1:?用法：e2e.sh <aos-daemon> <aos> <hello.so>}"
cli_binary="${2:?}"
plugin_path="${3:-}"

work_dir="$(mktemp -d)"
export AOS_SOCKET="${work_dir}/aos.sock"
if [ -n "${plugin_path}" ]; then
  export AOS_PLUGINS="${plugin_path}"
fi
# 別讓使用者自己 ~/.config 底下的外掛影響測試結果。
export XDG_CONFIG_HOME="${work_dir}/config"

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

"${daemon_binary}" &
daemon_pid=$!

# 等 socket 出現，不要用固定的 sleep（慢的機器上會偶發失敗）。
for _ in $(seq 1 100); do
  [ -S "${AOS_SOCKET}" ] && break
  sleep 0.05
done
if [ ! -S "${AOS_SOCKET}" ]; then
  echo "daemon 沒有起來" >&2
  exit 1
fi

echo "── 基本 ──"
check "ping" "pong" "$("${cli_binary}" ping </dev/null)"

"${cli_binary}" </dev/null >/dev/null 2>&1
check "沒給命令時 exit code 是 2" "2" "$?"

"${cli_binary}" daemon </dev/null >/dev/null 2>&1
check "分組節點的 exit code 是 2" "2" "$?"

# 命令清單要走 stderr（不是 stdout），這樣 `aos > out` 不會把錯誤混進輸出。
check "沒給命令時清單走 stderr" "" "$("${cli_binary}" </dev/null 2>/dev/null)"

echo "── stdin 的兩條路 ──"
check "echo 走 pipe" "hello" "$(printf 'hello' | "${cli_binary}" echo)"

printf 'from a regular file' > "${work_dir}/input.txt"
# 一般檔案：C++ 版因為 epoll 收不了它而需要另一條程式路徑，這一版沒有這個區別。
check "echo 走檔案重導向" "from a regular file" \
  "$("${cli_binary}" echo < "${work_dir}/input.txt")"

check "echo 走終端機以外的空輸入" "" "$("${cli_binary}" echo </dev/null)"

echo "── 二進位與大小 ──"
# 含 NUL 的資料要原樣來回。整條路上沒有一處靠 NUL 結尾。
printf 'a\000b\000c' > "${work_dir}/nul.bin"
check "含 NUL 的資料" \
  "$(cksum < "${work_dir}/nul.bin")" \
  "$("${cli_binary}" echo < "${work_dir}/nul.bin" | cksum)"

# 20 MiB 遠超過單一訊框 8 MiB 的上限，只有真的串流才過得了。
head -c 20971520 /dev/urandom > "${work_dir}/big.bin"
check "20 MiB 串流往返" \
  "$(cksum < "${work_dir}/big.bin")" \
  "$("${cli_binary}" echo < "${work_dir}/big.bin" | cksum)"

# ping 不讀 stdin。對方灌了 20 MiB 進來也要立刻回答，不能卡死。
check "ping 忽略大量 stdin" "pong" \
  "$("${cli_binary}" ping < "${work_dir}/big.bin")"

echo "── 常駐性 ──"
first="$("${cli_binary}" daemon status </dev/null | awk '/served/ {print $3}')"
second="$("${cli_binary}" daemon status </dev/null | awk '/served/ {print $3}')"
# 跨呼叫累加＝runtime 真的活在整個 daemon 生命週期裡，不是每條連線各一份。
check "daemon 狀態跨呼叫累積" "$((first + 1))" "${second}"

echo "── 並行 ──"
# 一條連線一條執行緒，所以這裡真的是同時在跑。少了鎖的話 daemon 的計數
# 會算錯，或更糟——直接壞掉。
parallel_pids=()
for i in $(seq 1 16); do
  "${cli_binary}" echo < "${work_dir}/input.txt" > "${work_dir}/par-${i}.out" 2>/dev/null &
  parallel_pids+=($!)
done
# 只等這些，不要用裸 wait（那會連 daemon 一起等下去）
for p in "${parallel_pids[@]}"; do wait "${p}"; done
intact=0
for i in $(seq 1 16); do
  if [ "$(cat "${work_dir}/par-${i}.out")" = "from a regular file" ]; then
    intact=$((intact + 1))
  fi
done
check "16 條並行連線都完整" "16" "${intact}"

echo "── 參數邊界 ──"
# 空白、空字串、引號都要原樣送達，因為送的是 argv 陣列而不是重新拼的字串。
# describe 拿到的是**完整的** argv（含命令名），因為根本沒對上任何命令路徑。
check "帶空白與空字串的參數" \
  "[0] nosuchcommand|[1] bot alpha|[2] |[3] say \"hi\"|" \
  "$("${cli_binary}" nosuchcommand 'bot alpha' '' 'say "hi"' </dev/null 2>&1 \
     | awk '/^  \[/ {printf "%s|", substr($0, 3)}')"

if [ -n "${plugin_path}" ]; then
  echo "── 外掛 ──"
  check "外掛載入了" "hello" \
    "$("${cli_binary}" daemon plugins </dev/null | awk '{print $1}')"
  check "外掛的命令進了命令樹" "hello" \
    "$("${cli_binary}" help </dev/null | awk '/範例外掛/ {print $1}')"
  check "外掛的子命令" "你好 世界" \
    "$("${cli_binary}" hello shout 你好 世界 </dev/null)"
  # 外掛拿到的是跟內建命令一樣的串流介面。
  check "外掛的串流" "ABC DEF" "$(printf 'abc def' | "${cli_binary}" hello upper)"
  check "外掛能讀到大量 stdin" \
    "$(cksum < "${work_dir}/big.bin")" \
    "$("${cli_binary}" echo < "${work_dir}/big.bin" | cksum)"
  "${cli_binary}" hello shout </dev/null >/dev/null 2>&1
  check "外掛的 exit code 傳得回來" "2" "$?"
fi

echo "── 收工 ──"
"${cli_binary}" daemon stop </dev/null >/dev/null 2>&1
for _ in $(seq 1 100); do
  kill -0 "${daemon_pid}" 2>/dev/null || break
  sleep 0.05
done
if kill -0 "${daemon_pid}" 2>/dev/null; then
  check "daemon stop 之後會結束" "結束" "還在跑"
else
  check "daemon stop 之後會結束" "結束" "結束"
  daemon_pid=""
fi
# 收工要把 socket 檔清掉，不然下次啟動會看到一個死掉的殘檔。
check "收工後 socket 檔不留下" "沒有" \
  "$([ -e "${AOS_SOCKET}" ] && echo "還在" || echo "沒有")"

echo
if [ "${failures}" -gt 0 ]; then
  printf '%d／%d 項檢查失敗\n' "${failures}" "${checks}"
  exit 1
fi
printf '%d 項檢查全過\n' "${checks}"
