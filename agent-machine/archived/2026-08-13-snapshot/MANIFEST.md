# Snapshot manifest

來源 commit：`32882d4`。原始大小以 UTF-8 bytes 計；除 `README.md` 前置三行 archive 提示外，23 份檔案的原文區塊均可與來源逐 byte 比較。

| 原路徑 | archive 路徑 | 分類 | 原始 byte size | 保存方式 |
|---|---|---|---:|---|
| `agent-machine/AGENTLOOP-ON-AOS.md` | [`AGENTLOOP-ON-AOS.md`](AGENTLOOP-ON-AOS.md) | AOS 草稿 | 4,903 | 原樣 |
| `agent-machine/AOS-ARCHITECTURE.md` | [`AOS-ARCHITECTURE.md`](AOS-ARCHITECTURE.md) | AOS 草稿 | 7,019 | 原樣 |
| `agent-machine/AOS-INTEGRATION.md` | [`AOS-INTEGRATION.md`](AOS-INTEGRATION.md) | AOS 草稿 | 3,688 | 原樣 |
| `agent-machine/AOS-SCHEDULING.md` | [`AOS-SCHEDULING.md`](AOS-SCHEDULING.md) | AOS 草稿 | 4,584 | 原樣 |
| `agent-machine/AOS-V0.md` | [`AOS-V0.md`](AOS-V0.md) | AOS 草稿 | 8,125 | 原樣 |
| `agent-machine/COMMANDS.md` | [`COMMANDS.md`](COMMANDS.md) | AgentOS prototype | 8,060 | 原樣 |
| `agent-machine/CONFIG.md` | [`CONFIG.md`](CONFIG.md) | AgentOS prototype | 4,937 | 原樣 |
| `agent-machine/DESIGN.md` | [`DESIGN.md`](DESIGN.md) | AgentOS prototype | 4,824 | 原樣 |
| `agent-machine/EXEC.md` | [`EXEC.md`](EXEC.md) | AgentOS prototype | 7,614 | 原樣 |
| `agent-machine/INTERFACE.md` | [`INTERFACE.md`](INTERFACE.md) | AgentOS prototype | 7,980 | 原樣 |
| `agent-machine/JSON.md` | [`JSON.md`](JSON.md) | AgentOS prototype | 3,528 | 原樣 |
| `agent-machine/LIMITS.md` | [`LIMITS.md`](LIMITS.md) | AgentOS prototype | 3,424 | 原樣 |
| `agent-machine/MEMBERS.md` | [`MEMBERS.md`](MEMBERS.md) | AgentOS prototype | 6,776 | 原樣 |
| `agent-machine/MESSAGES.md` | [`MESSAGES.md`](MESSAGES.md) | AgentOS prototype | 7,986 | 原樣 |
| `agent-machine/NOTES.md` | [`NOTES.md`](NOTES.md) | AOS 草稿 | 7,629 | 原樣 |
| `agent-machine/OPUS_ADVICE.md` | [`OPUS_ADVICE.md`](OPUS_ADVICE.md) + 7 份 `OPUS-ADVICE-*.md` | 思考／審查 | 39,414 | 拆分（完整；索引另建） |
| `agent-machine/OUTPUT.md` | [`OUTPUT.md`](OUTPUT.md) | AgentOS prototype | 5,023 | 原樣 |
| `agent-machine/PROCESS-NOTES.md` | [`PROCESS-NOTES.md`](PROCESS-NOTES.md) | AOS 草稿 | 6,466 | 原樣 |
| `agent-machine/README.md` | [`README.md`](README.md) | AOS 草稿 | 3,325 | 原樣（前置三行 archive 提示） |
| `agent-machine/RECOVERY.md` | [`RECOVERY.md`](RECOVERY.md) | AgentOS prototype | 3,816 | 原樣 |
| `agent-machine/RUNTIME.md` | [`RUNTIME.md`](RUNTIME.md) | AgentOS prototype | 6,700 | 原樣 |
| `agent-machine/SCENARIOS.md` | [`SCENARIOS.md`](SCENARIOS.md) | AgentOS prototype | 4,618 | 原樣 |
| `agent-machine/STORAGE.md` | [`STORAGE.md`](STORAGE.md) | AgentOS prototype | 6,600 | 原樣 |
| `agent-machine/TOOL_SPEC.md` | [`TOOL_SPEC.md`](TOOL_SPEC.md) | AgentOS prototype | 4,031 | 原樣 |

## OPUS split mapping

下列 byte range 為原始 `agent-machine/OPUS_ADVICE.md` 的半開區間 `[start, end)`。每頁的 `<!-- archive-original:start -->` 與 `<!-- archive-original:end -->` 之間是原文；移除導覽與標記後，按頁碼串接即可得到原始 39,414 bytes。

| 頁 | archive 檔 | 原始 byte range | 原始 bytes |
|---:|---|---:|---:|
| 1 | [`OPUS-ADVICE-01-OVERVIEW-AND-TOOLJSON.md`](OPUS-ADVICE-01-OVERVIEW-AND-TOOLJSON.md) | `[0, 6859)` | 6,859 |
| 2 | [`OPUS-ADVICE-02-COMPLEXITY-AND-FIXTURES-A.md`](OPUS-ADVICE-02-COMPLEXITY-AND-FIXTURES-A.md) | `[6859, 11063)` | 4,204 |
| 3 | [`OPUS-ADVICE-03-COMPLEXITY-AND-FIXTURES-B.md`](OPUS-ADVICE-03-COMPLEXITY-AND-FIXTURES-B.md) | `[11063, 15780)` | 4,717 |
| 4 | [`OPUS-ADVICE-04-VISION-AND-ONBOARDING.md`](OPUS-ADVICE-04-VISION-AND-ONBOARDING.md) | `[15780, 22414)` | 6,634 |
| 5 | [`OPUS-ADVICE-05-GAPS-DOCS-AND-BUILD-ORDER.md`](OPUS-ADVICE-05-GAPS-DOCS-AND-BUILD-ORDER.md) | `[22414, 28346)` | 5,932 |
| 6 | [`OPUS-ADVICE-06-LINUX-FILESYSTEM-AND-STORAGE.md`](OPUS-ADVICE-06-LINUX-FILESYSTEM-AND-STORAGE.md) | `[28346, 33582)` | 5,236 |
| 7 | [`OPUS-ADVICE-07-FILE-TOOLS-AND-OPEN-DECISIONS.md`](OPUS-ADVICE-07-FILE-TOOLS-AND-OPEN-DECISIONS.md) | `[33582, 39414)` | 5,832 |


## 核對規則

1. 對 22 份無提示副本直接做 byte comparison；`README.md` 移除前三行提示與其後空行，再與來源做 byte comparison。
2. 對七份 OPUS 頁擷取 `archive-original` 標記間內容，按頁碼串接，與原始 `OPUS_ADVICE.md` 做 byte comparison。
3. 所有 Markdown 必須是 strict UTF-8、無 BOM、單檔小於 8,192 bytes；所有相對 Markdown links 必須有有效目標。
