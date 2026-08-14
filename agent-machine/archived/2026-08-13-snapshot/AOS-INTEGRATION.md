# 工作目錄與 Task 狀態目錄

狀態：設計提案，尚未實作。

## AOS 核心只需要兩個使用者目錄

```text
工作目錄       Step 預設執行與讀寫的位置
Task 狀態目錄  呼叫端保存 Task 程式與持久狀態的位置
中央管理目錄   AOS 自己的 Task table、execution queues、Step 快照與執行收據
```

AOS 的 Task 記錄保存三者的對應。排程器只使用 Task 編號，不要求狀態目錄一定放在工作目錄裡，也不解讀
狀態目錄的內容。

## 狀態目錄是上層程式的記憶

AOS 是通用 Task 排程器，因此不規定狀態目錄一定包含 prompt、messages、tools 或 workflow。呼叫端可以直接
修改其中的程式與資料，再把新 Step 交給 AOS。修改只影響尚未提交的 Step。

agent loop 建立在 AOS 上時，可以沿用 `~/repo/workflows` 的做法，使用類似 `.claude/` 的 `.aos/`：

```text
.aos/
  README.md              入口；說明先讀哪裡
  WORKFLOWS.md           依目前意圖選擇工作流
  workflows/             各工作流自己的規則、知識與子工作流
  SESSION-LOG.md         尚未完成的進度；完成即移除
  WAIT_USER.md           等使用者處理的事項；完成即移除
  inbox/                 Task 或 agent 之間的訊息，需要時才建立
```

這是 agent loop 層的慣例，不是 AOS 核心 ABI。其他 AOS 使用者可以採用完全不同的內容。

工作流文件仍遵守幾個簡單原則：

- 上層只負責導向下一層，不複製下層細節。
- 長期知識放在所屬工作流；正在進行的狀態只保留未完成項目。
- 小工作流先用單檔，內容變多才改成資料夾。
- 工作流文件超過 8192 bytes 時檢查是否能依職責拆開。

## 放在工作目錄內：侵入式

```text
project/
  .aos/
    README.md
    WORKFLOWS.md
    workflows/
  ...project files...
```

這種方式讓 Task 程式、記憶與工作資料一起移動、複製及進 Git。建立前必須列出將新增的路徑；遇到既有檔案
就拒絕，不能默默覆寫。

queue position、running Step 等中央管理資料不放進 `.aos/`，避免複製目錄時連執行權一起複製。若需要
`start`、`status` 或 `task` 等同目錄入口，可以建立呼叫中央 `aos` 命令的薄 wrapper；wrapper 不保存
排程狀態。

## 放在工作目錄外：非侵入式

```text
工作目錄：     /work/existing-project
Task 狀態目錄：/data/project-aos
```

```sh
aos task create --workdir /work/existing-project \
  --state-dir /data/project-aos
```

AOS 不在既有專案中新增 `.aos`、wrapper、lock 或 log。中央 Task 記錄保存兩個目錄的對應；Step 執行時仍
取得明確的 cwd。這不是 union filesystem，也沒有路徑覆蓋規則。

非侵入式的基本驗收是：若 Step 本身沒有修改工作目錄，Task 建立、執行與移除前後，工作目錄必須完全相同。
Step 若要修改工作檔案，則必須是該次工作的明確效果，不能是 AOS 偷放管理檔。

這種方式適合既有 repo、唯讀資料、暫時工作目錄，或不希望狀態檔進入原專案版控的情況。

## 兩種放法使用同一套排程

兩種方式只有狀態目錄的位置不同。Task／Step 格式、execution queues 與排程規則完全相同。scheduler 的
每個候選包含：

```text
Task 編號與 priority + 下一個 Step 與 priority + target queue + 可用 executor
```

manager 執行 Step 時才解析出工作目錄與狀態目錄。這讓 AOS 可以像外部 supervisor 一樣完全不碰既有目錄，
也可以深入成為專案的一部分，而不需要兩套 runtime。
