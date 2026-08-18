# 規格 3:串流支援

狀態:**討論中定案,尚未實作**。與 [SPEC_env_1](SPEC_env_1.md)、
[SPEC_exec_1](SPEC_exec_1.md) 同屬 inst_t → 執行的重整。承接
[DISC_CONC_1](DISC_CONC_1.md) 對串流的初步結論。

## 消費模型:外部觸發 + 每次 drain-to-EOF(定案)

aos-c **無狀態、不常駐**。由外部(cron 或未來呼叫 ABI 的程式)**週期性觸發**,
每次跑就「吃串流吃到 EOF 就收工」。「持續下去」這件事外包給觸發者,不放進
aos-c。

- **好處**:沒有 daemon 那套(重開、poll、收工信號全免);解析失敗的「永久
  死亡」自動退化成「下次觸發從頭再來」的自癒;符合 Unix 分工。
- `waitpid` 仍保留(循序、收殭屍、取 status),省不掉的只是 daemon 迴圈。

## 契約(成立前提)

- **EOF = 生產端關閉。** drain-to-EOF 唯一的結束信號就是 EOF,而 EOF 只在寫入
  端全關時才來。所以生產端必須是「**開 → 寫一批 → 關**」的批次/管線型
  producer(`A | aos-c` 正是如此)。
- **有人把 FIFO 開著猛灌、從不關 → 使用者的事。** aos-c 卡著等 EOF 是對方違約
  的後果,不是 aos-c 要防的。
- **單一消費者由使用者紀律保證。** 同一條 FIFO 同時兩個 aos-c 會搶記錄、讀到
  半筆而錯位;aos-c **不**加 `flock`,這是部署紀律。

## 來源

- **stdin / pipe**:`argc < 2` → 讀 `std::cin`(`A | aos-c`)。
- **路徑**:`argc == 2` → **直接開 `argv[1]`**(可能是普通檔,也可能是 named
  FIFO)。`argv[1]` 就是檔/FIFO 路徑本身;`XXX/.aos/insts` 的組路徑交給上層
  使用者,aos-c 不碰(那個位置日後可能改,更不該寫死)。

## 實作

- 現有 `run_stream` 骨架(逐字元 `in.get()` 讀 + 讀一筆跑一筆)本來就串流友善,
  **不改**。FIFO 空著時 read 自然阻塞、pipe buffer 滿時寫入端被擋、循序執行天然
  節流——都免費。
- **把 `argv[1]` 的 `std::ifstream` 換成「自己 `open()` 拿 fd + fd-streambuf」**
  (照 `capi.cpp` 的 `FileBuf` 抄一個吃 fd 的版本)。理由是兩個必要旗標
  `std::ifstream` 都設不了:
  - `open(argv[1], O_RDONLY | O_CLOEXEC | O_NONBLOCK)`
    - **`O_CLOEXEC`**:免得這個 insts 讀端漏進每個子程式;在 FIFO 下漏了會讓
      上游永遠等不到「沒讀者」而掛死(DISC_CONC_1 結論五)。
    - **`O_NONBLOCK`**:開 named FIFO 時,若當下沒有寫入端,一般開法會**卡在
      `open()`**;加上它就立刻成功,不卡。
  - **開完立刻 `fcntl` 清掉 `O_NONBLOCK`**,讓後續 read 回到阻塞模式。清掉後
    FIFO 的 read 語意剛好對:沒寫入端 → 立刻 EOF → 秒退;有寫入端沒資料 →
    阻塞等;寫入端關閉 → EOF → 排空完成。普通檔上 `O_NONBLOCK` 是 no-op。
- **stdin 來源不需要這段**:它已是 fd 0。只有 `argv[1]` 自己開的 fd 要
  `O_CLOEXEC`。

## 解析失敗

- 維持現況:任一筆解析失敗 → 印錯 + **停止整條串流**(八行格式無記錄分隔符,
  錯位無法重新對齊;見 SPEC_exec/DISC_CONC_1)。在本模型下這只是結束這次觸發,
  下次觸發重來,無傷。**不**為了容錯上 framing——那是格式升級,YAGNI。

## 一個要記住的 caveat:stdin 來源時子程式的 stdin

當 insts 走 **stdin**、而某筆 inst **沒有指定 `stdin_path`** 時,子程式會繼承
aos-c 的 stdin——也就是**那條 insts 串流本身**,於是子程式會去吃本該給 aos-c 的
記錄。

- 對策(擇一,屬使用者責任,非 aos-c 邏輯):餵 stdin 的那批 inst 各自指定
  `stdin_path`;或改走 **`argv[1]` 路徑/FIFO** 來源——後者的 insts fd 是獨立的
  (且有 `O_CLOEXEC`),子程式的 stdin 與 insts 互不相干,天生沒這問題。
- 因此:**串流建議優先走 `argv[1]` 路徑/FIFO**,stdin 來源留給「每筆都自帶
  stdin_path」或臨時管線。

## ABI

依決定,**ABI 留到最後統一規劃**。本階段不加 `aos_run_stream`;C ABI 維持逐筆
積木(`aos_instruction_read` + `execute`),要整條跑的呼叫端自組迴圈。等出現真實
呼叫端、知道它要的回報形狀(整體碼 / 逐筆 callback / 計數)再設計。

## 驗證
- WSL Ubuntu:`make clean && make test`,全綠、零警告。
