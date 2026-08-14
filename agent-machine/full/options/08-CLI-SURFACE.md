# CLI 表面選項

狀態：**尚未決定**。以下命令只用來比較語意，全部都是示意，不是名稱、旗標、exit code 或 JSON ABI。主線只固定使用者偏好 shell／`$PATH`，以及所有方便入口最後要降成同一份已驗證 Call。

共同流程是：client 解析 Agent root、Function Definition、argv、stdin、cwd 與環境規則；AOS durable accept 後才回 Task reference；client 可等待同一 Task，也可離開。詳見[Shell／CLI 主線](../11-SHELL-CLI-AND-PATH.md)。

## 方案一：完全明示的基準入口

示意：`aos exec --agent ABS_ROOT -- ABS_DEFINITION ARG...`。

- **優點**：Agent、Function 與參數邊界最清楚；不依賴 cwd／PATH 順序；適合測試與機器呼叫。
- **缺點**：日常太長；使用者要先知道絕對 path；多行 prompt 與 stdin 仍需設計。

它應作 semantic baseline，不一定是主要 UX。

## 方案二：在 Agent root 內呼叫

示意：先 `cd bot-a`，再用 AOS client 呼叫 `./ask`。client 使用已明確建立／attach 的 root context，不向父目錄猜最近的 `.aos`。

- **優點**：最接近使用者習慣；短而清楚；file Function 長成同名 directory Function 時可維持目標 path。
- **缺點**：root 宣告、Definition 入口與預設 cwd 尚需決定；普通目錄不能因剛好有 `.aos` 就被猜成 Agent。

這是日常介面的推薦方向，不是最終拼法。

## 方案三：PATH 中的短控制代理

進入 root 後，將查 Task、追加、排隊等小型 wrapper 放到 PATH；wrapper 只攜帶固定 root context，仍呼叫同一 AOS client。

- **優點**：留在原 shell；操作最短；可讓 Agent 提供自己的輔助入口。
- **缺點**：兩個 roots 的同名 command 可能被舊 PATH 送錯；wrapper 容易長出第二套 parser；直接把 Function 當普通 PATH executable 會繞過 durable accept。

第一版若採，只提供控制代理；Function 執行仍走共同 Call 路徑。

## 方案四：`interact` 類型的 nested shell

示意入口開啟使用者原本的 interactive shell，固定 Agent identity、prompt 與 PATH context。

- **優點**：目前地盤一眼可見；進入／離開 PATH 有清楚邊界；可日後接 REPL。
- **缺點**：shell 巢狀、TTY、遠端與 Ctrl-C 複雜；普通 child process 不能修改 parent shell；太早做會把 durable Call 和即時互動混在一起。

等 wait／detach 與 root attach 穩定後再試。

## 所有表面都要相同的語意

- 成功回覆前，Call 已被持久接受；前景與 detach 只差 client 是否等待，不產生兩種 Task。
- Ctrl-C／terminal 關閉只離開等待，不等於 stop；重新連線等待舊 Task 不得建立新 Task。
- 普通 Agent 呼叫遇 busy 直接拒絕且零 Task；追加目前 Task與明確排隊必須是不同操作。
- `--`、以 `-` 開頭的 prompt、空白與特殊字元按 argv 傳遞，不經 shell 重解析。
- 相對 path 與 cwd 在接受前依明確 context 解析；保存後不因 client `cd` 而改義。
- 人類輸出與 machine output 可不同呈現，但共享同一驗證與狀態轉移；目前不固定欄位或 exit code。
- 直接執行 `./ask` 可供除錯，但要明說它繞過 Task 保存、排程與 unknown 保護。

## 目前推薦

先以方案一凍結**測試語意**，再讓方案二使用同一實作做兩至五分鐘日常流程；方案三只加薄控制代理，方案四延後。這個順序不凍結 `exec`、`run`、`call`、`task` 或 `interact` 等字串。

## 驗證與推翻條件

1. parser corpus 覆蓋 `--`、空 argv、以 dash 開頭值、raw stdin 與含空白 path，Function 收到的 bytes 不變。
2. 從不同 cwd 呼叫同一 Definition，保存的 root、Definition 與 cwd 明確且可重現。
3. foreground、detach、Ctrl-C、重連 wait 全程只有一個 Task 與一次 effect。
4. busy 三路分開測；任何方便 wrapper 都不能把拒絕改成排隊。
5. 兩個 Agent 有同名 PATH 代理時不得送錯 root；離開 context 後 PATH 可可靠還原。
6. 讓不熟文件的人完成一次呼叫、查看、追加與明確排隊；若必須理解 attempt／hash 才能操作，UX 方案失敗。

若方案二無法在不猜 root 的前提下保持短小，就保留明示基準並重新比較 attach context；若 nested shell 的 TTY／中斷行為不能和 durable Task 分開，便不升為預設。原始候選見[CLI 候選](../../workbench/2026-08-14/idea-ledger/CLI-CANDIDATES.md)。
