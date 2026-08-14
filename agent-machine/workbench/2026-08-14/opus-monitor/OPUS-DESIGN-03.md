# OPUS-DESIGN-03：AOS 單機主線設計

## 結論

四題其實是同一題。把 **agent root 的寫入權租約（lease，一次只有一個持有者的可寫授權）** 當成唯一的序列化原語，其餘三題就會長成一致的形狀：接受即發身分、租約綁地盤而不綁 Task、child 繼承 parent 的租約、記憶只在發布點換版、`.aos` 切成可攜與本機兩半。

## 起點與新增

**使用者已定（不動）**：Function 像指令、filesystem 像記憶體；資料夾可以是 Function；`aos exec ./ask "..."`；agent root 的絕對路徑就是 agent ID 與地盤；避免多實例；root 要能直接上 GitHub；偏好 `.aos`。

**我新增（可反對）**：租約是唯一序列化原語；child Task 繼承租約；「入口」與「哪些檔案算 Definition」是兩個獨立問題；`.aos/local/` 是唯一一條 `.gitignore`；重啟後以租約世代判斷可否續接。

## 推薦模型

### 一、身分：接受就發，永不換

Call 通過驗證並安全落盤後立刻取得 `task_id`；此後排隊、執行、等 child、暫停、重啟、完成紀錄都用同一個 ID 與目錄。實際啟動 process 只是內部的一次 attempt（執行次數），一般畫面不顯示。使用者只說「正在執行中的實例叫 Task」；我把「執行中」當成它的一個狀態而非全部生命期，這是延伸，要照實標。

反例（為什麼不能等 dispatch 才給身分）：

```sh
aos exec ./ask "寄週報"     # 已回「已接受」
# manager 在 dispatch 前被殺
aos task list               # 這個工作現在叫什麼？
```

沒有落盤身分，重啟後只能重新解析命令列，而它已經不在了。

### 二、忙碌：租約綁地盤，child 繼承

`bot-a` 一次只有一棵可寫 Task tree。外來的可寫 Call 預設**排隊**，不是回覆忙碌——那等於把重試邏輯推給每個呼叫端。宣告唯讀者不取租約、可並行，但不得發布記憶。

反例（把「一個地盤一個主事者」實作成「每個 Task 各自搶租約」會死鎖）：

```text
T1 (ask) 持有 bot-a 的租約
T1 呼叫 ./ask/functions/apply -> child T2
T2 排隊等租約 -> 等 T1
T1 等 T2 -> 永遠不會結束
```

所以租約的持有者是**整棵 tree**，不是單一 Task；child 直接繼承。取消與 pause 也以 tree 為單位。

唯讀要誠實：它是「宣告 + 不給發布權」，不是 sandbox。`./ask --dry-run` 若偷偷寄信，AOS 擋不住，只能保證記憶沒被改。

### 三、記憶分區：三條規則就夠

| 資料 | 規則 | 位置 |
|---|---|---|
| Function 定義 | 讀目前路徑，dispatch 前比對版本 | `ask`／`ask/call` |
| agent 記憶 | 寫暫存，比對 base 版本後原子發布 | `.aos/memory/` |
| 執行證據 | 一旦提交不再修改 | `.aos/tasks/` |
| 本機狀態 | 不進 Git，重啟即失效 | `.aos/local/` |
| 外部世界 | intent → 執行 → Receipt，不宣稱可回滾 | 無 |

版本世代只需兩個：`definition_generation`（用哪版程式）與 `memory_generation`（記憶到哪版）。「這次 AOS 啟動」不必另立名詞，它是租約上的一個欄位。

**重啟後怎麼判斷可安全續接**：舊租約一律失效，再逐一 Task 檢查——有完整 Receipt 就是完成；只有 dispatch intent 就是結果不明，停住等人決定；連 intent 都沒有就可安全重發，因為能證明沒有外部作用。不必問 PID。

反例（`.aos` 不能整包進 Git，也不能整包不進）：

```sh
git clone .../bot-a && cd bot-a
cat .aos/local/lease      # 若進了 Git：寫著別台機器的 PID 4242
aos task list --finished  # 若整包不進：一片空白，agent 失憶
```

切兩半，`.gitignore` 只需一行 `.aos/local/`。clone 過來沒有租約也沒有本機狀態，預設未連接、零自動執行。

### 四、成長：四層，只有兩層改變 AOS 看到的東西

```text
0  bot-a/ask*                      一個檔
1  bot-a/ask/call*                 目錄化；仍是一個 Task
2  bot-a/ask/functions/apply/call  需要獨立 pause／恢復／外部作用邊界時，才成為 child Task
3  bot-a/subs/writer/              需要自己的記憶、佇列與 Git 歷史時，才成為 child agent root
```

層 0→1 不改命令也不多出 Task。層 2 有 child Task 但共用租約。層 3 才有第二份租約與第二個 `.aos/`，且須由 parent 明確登記，AOS 不掃 `subs/` 猜誰是 agent。

入口與版本是**兩個問題**：`call` 這個固定檔名回答「怎麼跑」，入口設定檔（manifest）回答「哪些檔案算 Definition」。

```text
ask/
  call
  main.py        # 假入口：AOS 必須只跑 call
  lib/render.py  # 改了它，若版本只 hash call，版本號不變
```

第二種更糟：兩個 Task 宣稱同版程式、實際行為不同，重現性是假的。有 helper 的那一刻就需要 manifest，沒有時可以不寫。

### 四題怎麼互相牽動

有落盤身分（一）才可能排隊（二）；有租約（二）才敢讓 root 保持 live（三），因為同時只有一棵 tree 能寫；有 manifest（四）才有可信的 `definition_generation`（三）。成長的每一層正好是前三題規則改變的位置。

## 可採用的替代方案

| 方案 | 優點 | 代價 | 適用時機 |
|---|---|---|---|
| **推薦**：live root + 租約 + `.aos` 兩半 | shell 直覺不變；可攜；規則少 | 每類資料要標清楚規則；租約協定要寫對 | 單機、單 agent 的第一版 |
| **中央 store 權威**：全部存 `/var/lib/aos/`，root 只放程式 | 單一 writer 最好做；查詢快 | clone 後 agent 失憶；違反「root 可上 GitHub」 | 多 agent 運維、不在意可攜 |
| **常駐 worker**：每 agent 一個常駐 process，Call 進信箱 | 序列化天然成立，無租約協定，也不會 child 死鎖 | 多一個要顧生命週期的常駐 process；它掛掉等於 agent 掛掉 | 真的做中央常駐 Runtime 時 |

常駐 worker 是最可能取代推薦案的對手：等常駐 Runtime 存在，租約會退化成信箱的實作細節。

## 原型順序

Python 先跑對語意，C++ 只重驗 Linux 會咬人的邊界，Janet 只出建議。

| # | 語言 | 做什麼 | 跑完可決定 |
|---|---|---|---|
| 1 | Python | 租約：child 繼承、外來可寫排隊、唯讀不排隊、Ctrl-C 不釋放租約 | 第二題 |
| 2 | Python | 接受落盤即發 ID；accept 後 dispatch 前殺 manager，ID 不變且外部作用 0 次 | 第一題 |
| 3 | Python | `ask` → `ask/call` 命令不變；改 message 不換版、改 helper 換版；假 `main.py` 不被跑 | 第四題前半 |
| 4 | Python | `.aos` 兩半；clone 後零自動執行；舊世代的執行中 Task 不自動續跑 | 第三題 |
| 5 | C++ | `fork`／`exec`、fd 與檔案鎖的繼承、fsync／rename／目錄 fsync 次序、torn tail、拒絕 symlink 與 FIFO | C++ 是否擁有全部 durable commit |
| 6 | Janet | 兩 process 用固定 JSON：只讀已驗證快照回建議；偽造 Receipt 或過期版本零寫入 | Janet 的責任邊界 |

第 5 步的關鍵是**子 process 會不會繼承 store 的檔案鎖**。fd 沒設 `CLOEXEC`（exec 時自動關閉），被殺的 writer 留下的孫代會繼續持鎖，重啟的 AOS 卡住或誤判「還有人在寫」；設了，則要面對「舊 process 還活著、鎖已釋放」。兩種都要測，Python 測不出來。

Janet 之前先過工具鏈檢查：WSL 目前沒有可用的 Linux Janet，缺件就回報，不要用 Python 冒充。

## 建議改掉、延後或改名

**改掉**：把「固定 `call`」與「manifest」當成二選一（應並存）；把「一個地盤一個主事者」實作成每個 Task 各自搶租約（必須 child 繼承）；在「唯讀」有可驗證定義前就寫成保證。

**改名**：對外只講 Task 與 `aos task list --finished`，attempt 只在診斷出現，不要另造「已完成 Task 的紀錄」這個公開名詞；不要第三個 generation。

**延後**：雙 ID 比較原型；branch／overlay、SQLite、path 搬移、跨機器、Step 原子邊界；C++ 內嵌 Janet 與 FFI，先只做兩 process 接法。
