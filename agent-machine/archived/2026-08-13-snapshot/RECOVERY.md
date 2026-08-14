# AgentOS instruction 與恢復契約

本頁接續 [`RUNTIME.md`](RUNTIME.md)，固定一條 LLM／tool instruction 如何落盤，以及 worker crash 後何時
可以繼續、何時必須停下。

## 一條 instruction 的落盤順序

worker 每次只做以下流程：

1. state lock：選出下一件事，append `prepared` event，保存完整 input、`mode` 與 `return_to`，state 變 working。
   automatic worker 的 `return_to` 是 ready；`run next` one-shot worker 的 `return_to` 是 paused。
2. fsync journal 與 snapshot，release state lock。
3. state lock：append + fsync `dispatching`，release；然後呼叫一次 LLM 或一個 tool。event 與真正送出的
   瞬間無法跨 process atomic，所以 crash 一律保守視為可能已送出。
4. executor 回來後，先把 immutable result 寫入 `results/<instruction-seq>.json` 的 temp file，fsync、atomic
   rename、fsync directory；result 含 instruction seq 與 input hash，寫好後不再修改。
5. state lock：驗證 result，append + fsync `finished` event，再更新 messages、usage、pending tools 與 snapshot。
6. 處理已排入的 pause／stop；只有 mode 仍是 auto 才開始下一輪。

journal 是恢復真源；state 與 messages 是方便讀取的 snapshot。每個 mutation 都先 append journal 並 fsync，
再寫 temp snapshot、fsync、atomic rename、fsync parent directory。crash 後以 journal replay 補齊 snapshot。

工具已開始後不能宣稱 rollback。任何 callback／policy 只能提出下一個 command，不得繞過上述流程直接改
messages 或 state。

## pause、stop 與 wait

working 時的 pause／stop 在 state lock 下寫入 `after`，然後 client 等 state 改變。worker 完成本條
instruction 後才套用；等待 client 被 Ctrl-C 不會撤回已接受的 request。

`run wait` 是 client，不是 worker。v1 可以用 inotify 等 state 檔變化，無法使用時退回低頻 polling。
每次進入新的 waiting reason 只提示一次，不洗畫面；`--json` 不用來串流，程式可輪詢 `status --json`。

## process crash

worker lock 隨 process 死亡由 Linux 自動釋放。看見 working 不能直接修：先 non-blocking try worker lock；
busy 代表 live worker 仍擁有執行權，只讀狀態。取得 lock 後才可在 state lock 下依 journal/result 修復：

- 只有 prepared、沒有 dispatching：executor 肯定未送出。先套用 prepared 之後已接受的 control command；
  `after:stop` 進 stopped，`after:pause` 進 paused，否則回到 prepared 保存的 `return_to`。因此 automatic work
  回 ready，`run next` 仍回 paused，不會因 crash 偷偷改成 auto。
- result 已完整落盤：完成尚未做完的 state commit，不重呼 executor。
- 已有 dispatching、沒有可靠 result：failed + `outcome: unknown`，不自動重送。

修復本身也在 state lock 下 append event。journal 最後一筆若只有半行，可截掉該半行；中間 event 缺號、
JSON 損壞或 hash 不符就 fail closed，不猜測。如此不靠 PID 猜 process 是否仍活著，也不把 Git commit
當成 LLM/tool 外部副作用的 transaction。

## detached worker 的誠實邊界

`continue` 先 durable 保存 `mode:auto` 再 spawn。若 client 恰好死在兩者中間，工作可能保持 ready/auto
但沒有 worker；下一次 `run continue` 會安全補 spawn。若未來要求 reboot 後無人操作也自動續跑，才增加
per-user service／systemd registry。

v1 不宣稱有跨 Bot scheduler。不同 Bot 由 Linux 排程；目前只保證 per-Bot single executor 與 local-model
resource lock。token、確定性、foreground UX、priority 與 memory pressure 的中央排程留給 C++ per-user
service，屆時由 scheduler 持有同一把 worker lock，公開 CLI 不變。
