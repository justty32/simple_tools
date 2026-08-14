<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-04-VISION-AND-ONBOARDING.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-06-LINUX-FILESYSTEM-AND-STORAGE.md)
<!-- archive-nav:end -->

<!-- archive-original:start -->
## 六、幾個具體的洞（不大，但會咬人）

**`run wait` 在沒有 TTY 時碰到 `waiting` 會永遠卡住。** `OUTPUT.md` 說「waiting 時顯示所需動作並繼續等」。
在 CI 或 `nohup` 裡這就是掛死。建議：無 TTY 且進入 `waiting` 時印出問題並 exit 1，
要阻塞的人明確加 `--wait-for-answer`。

**history 沒有出口。** messages 屬於 Bot、`start "more"` 一直延續、`LIMITS.md` 明確不做 token 上限 ——
所以一個長期使用的 Bot 最後一定會撞到 context window，然後得到一個看不懂的 400 錯誤。
v1 不必做摘要或截斷，但至少要：`status` 顯示 history 的約略 token 數；
撞到 context 上限時的錯誤訊息直接說「history 太長，執行 `./messages clear`」。

**`messages clear` 的語意會嚇到人。** 文件說 clear 會「從 source list 移除非 system messages」——
也就是會刪掉使用者用 `messages use` 明確設定的 context refs。使用者對 clear 的預期是「忘掉對話」，
不是「刪掉我的設定」。建議：`clear` 只清 history；要動 source 一律走 `clear --all`。

**tool 的 cwd 沒有解答「Bot 放哪裡」。** cwd = Bot root，那要檢查一個專案時，Bot 得放在專案裡的子目錄，
所有相對路徑都變成 `../src/...`。文件沒有 workspace 概念，但 `CONFIG.md` 又提到「未特別指定 workspace 的
tool」，暗示有這個東西。建議明確化：Bot 有一個 workspace 設定，預設是 Bot root，
`_extra.cwd: null` 表示「用 Bot 的 workspace」。一個旋鈕、一個預設值，寫一行就講完。

**exit code 1 同時表示「狀態拒絕」和「工作失敗」。** 腳本會想分辨。`--json` 的 `error.code` 有，
但 text mode 沒有。可以考慮拒絕用 3、失敗用 1，或至少在文件裡明說要分辨就用 `--json`。

**journal 與 archive 沒有保留政策。** 每個 Bot 的 `journal.jsonl` 和 `archive/` 無限成長。
一行話的政策就夠（例如 archive 只留最近 N 次）。

---

## 七、一個很小、但值得加的安全設計

現在的安全模型是二選一：auto mode 全自動跑所有工具，或 `--step` 每一條都要按 `next`。
中間沒有東西，而中間才是日常。

建議在 tool spec 加一個 boolean：

```json
"_extra": {"_type":"exec", "confirm": true, ...}
```

auto mode 碰到 `confirm: true` 的 tool，在 dispatch 前停成 `paused + next: tool`。
使用者 `run next` 放行或 `run skip "理由"` 拒絕。

**它不需要任何新狀態、新指令或新機制** —— 完全重用既有的 pause/next/skip。
一個 boolean 就讓「讀檔自動跑、寫檔和 shell 要問我」成立，這是沒有 sandbox 的情況下
CP 值最高的一道防線。

---

## 八、文件本身：17 份 → 6 份

`DESIGN.md`、`INTERFACE.md`、`COMMANDS.md`、`SCENARIOS.md` **四份文件在描述同一組指令**，
加起來約 600 行，只是詳細程度不同。任何一次介面調整要改四個地方，它們一定會漂移。
（現在其實已經開始漂了：`DESIGN.md` 的 run 動詞表沒有 `more`，`INTERFACE.md` 的也沒有，
但 `COMMANDS.md` 和 `LIMITS.md` 有。）

建議合併成：

| 文件 | 併入 |
|---|---|
| `README.md` | quickstart（≤6 個指令）＋ 一段模型說明 ＋ 三個連結。目前的舊命令那段直接刪掉 |
| `INTERFACE.md` | ＋ DESIGN ＋ COMMANDS ＋ SCENARIOS ＋ OUTPUT ＋ LIMITS |
| `MEMBERS.md` | ＋ MESSAGES ＋ STORAGE ＋ CONFIG ＋ JSON 的讀取規則 |
| `TOOLS.md` | 取代 EXEC ＋ TOOL_SPEC ＋ JSON 的 schema 子集（理想上只寫「差異表 + 引用 tooljson」） |
| `RUNTIME.md` | ＋ RECOVERY |
| `NOTES.md` | 不動。它的定位（明講「不是契約」）是全部文件裡最健康的一份 |

還有一件事：現在**每一份文件都用契約口氣寫**，但實際上只有極少數行為被實作過。
建議在每份文件開頭放一個明確標記：

```text
狀態：已實作 / 已凍結 / 提案中（實作後可能推翻）
```

`NOTES.md` 已經這樣做了，其他 16 份沒有。

---

## 九、施工順序：建議把它倒過來

`docs/freepy/agent-machine/04-python.md` 的 P0→P3 是「先做 namespace/generation/CAS/journal/Git 三個 PR，
過關之後才接真 LLM」。理由（避免模型延遲掩蓋資料一致性問題）在工程上成立，但代價是：

**在使用者能跟模型講第一句話之前，要先蓋三個 PR 的機械。**

以 KISS 和「早點知道介面對不對」來說，我建議倒過來：

```text
第 1 步   new → llm set model → start "hi" → 印出答案
          單一 state.json + append-only journal，atomic rename（~30 行，不是負擔）
          沒有 worker、沒有背景、沒有 lock：前景同步跑完

第 2 步   tools（直接用 tooljson）+ 一個 exec tool 真的跑起來
          --step / next / skip / status

第 3 步   detached worker、flock、pause/continue/stop、crash 修復

第 4 步   $ref / $env 組合、shared config
```

理由：整個提案的價值主張（「Bot 是一個可以直接跑的目錄」）在第 1、2 步就能被驗證或推翻，
大約 300 行。而現在被寫死的那些 crash 語意（`prepared` vs `dispatching`、`outcome: unknown`、
三層 lock ordering）保護的是單人使用時幾乎不會遇到的情境，卻佔了規格 40% 的難度。
journal + atomic rename 從第一天就做（便宜、事後補很痛），其餘的修復機制留到第 3 步。

另外：Windows 上開發時，請刻意保持「純」的部分（JSON 讀取、composition、驗證、argv 組裝）
不依賴 OS，讓 `_checks` 在 Windows 原生就能跑，只有 flock/fork/exec 需要 WSL。
`docs/.../04-python.md` 的 P1 gate 已經這樣寫了，但 `agent-machine/README.md` 目前是一句
「Linux-only、一律從 WSL 執行」。這會讓你自己每天多一層摩擦。

---

<!-- archive-original:end -->

<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-04-VISION-AND-ONBOARDING.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-06-LINUX-FILESYSTEM-AND-STORAGE.md)
<!-- archive-nav:end -->
