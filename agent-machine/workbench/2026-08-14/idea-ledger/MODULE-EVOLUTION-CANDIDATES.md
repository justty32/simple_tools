# Function 與 Agent 成長候選

> 本頁整理 `module_evolution_round2` 支線。以下是演進路線，不是已定布局或 ABI。

## 三種成長不要混在一起

同一份工作變大時，可能發生三件不同的事：

1. **單檔變資料夾**：只是整理程式結構，對 AOS 仍是一個 Function Call，也不會因此多出 child Task。
2. **Function 拆成多個 Functions**：工作需要獨立的 pause、recovery、結果或外部作用邊界，才由組合型（composite）Function 明確呼叫 child Functions，形成 child Tasks。
3. **模組升成 child agent**：它需要自己的 memory、queue、checkpoint、Git 歷史或互動入口，才成為有自己地盤的 agent root。

目錄很大，不代表它一定是 composite Function；有很多 helper，也不代表它是 agent。

## 由小到大的四層

### 層級 0：Leaf Function

```text
bot-a/
  ask*               # executable file
```

`aos exec ./ask ...` 最後形成一次 process 呼叫。

### 層級 1：Directory Function

```text
bot-a/
  ask/
    call*             # 固定入口候選
    lib/
    prompts/
```

使用方式仍是 `aos exec ./ask ...`。`call` 可載入 helpers，但整體仍是一個 process leaf；目錄化只解決檔案太多，不改 Task 結構。

### 層級 2：Composite Function

```text
bot-a/
  ask/
    call
    functions/
      inspect/call
      apply/call
```

外層 `ask` 明確呼叫 `inspect`、`apply` 等 Functions。只有需要讓 AOS 分別保存、暫停或恢復時，才把它們做成可見 child Tasks。

### 層級 3：Child agent root

某一塊工作開始需要自己的記憶、排程、checkpoint、Git 與互動介面時，由 parent 明確登記為 child agent。AOS 不掃 `subs/` 猜哪些子目錄是 agent。

Child agent 有自己的寫入權與生命週期。Parent 透過 Function Call 合作，不直接把它當普通 helper 目錄修改。

## Directory Function 的四種入口

### 方案 A：固定 `call`

```text
ask/call
```

優點：最簡單、無需先解析入口設定檔（manifest）；leaf `ask` 變成 directory `ask/` 後，呼叫路徑不變。

缺點：只能表達單一預設入口；其他型別、版本與依賴範圍要放在別處。

建議：第一個原型先試這案。

### 方案 B：Manifest 指定入口

```text
ask/
  function.toml
  bin/ask-main
```

優點：能明列入口、型別、版本與 Definition inputs；日後較容易擴充。

缺點：manifest 與實際檔案可能不同步；解析失敗必須在任何外部作用前拒絕。

### 方案 C：保留薄 launcher，加另一個 module 目錄

```text
ask*                  # 只負責進入模組
ask.module/
  ...
```

優點：shell 仍把 `./ask` 當普通 executable；可逐步搬移舊程式。

缺點：一個 Function 有兩條路徑，移動、Git 歷史與 Definition 身分較難說清楚。

### 方案 D：使用 `.aos` manifest 檔

```text
ask.aos
ask/
  ...
```

優點：AOS Function 一眼可辨，入口與設定都能明示。

缺點：日常命令可能變成 `aos exec ./ask.aos`，改變使用者偏好的 `./ask` 外觀；也仍有雙路徑問題。

## 共同解析規則候選

- TARGET 先依命令列端目前目錄轉成絕對 Definition path。
- file 只執行該 file；directory 只依選定的固定入口或 manifest。
- 不搜尋 `$PATH`、不猜 `main.py`／`run`，也不掃整個目錄碰運氣。
- AOS 接受這次 Call 時保存已解析入口與版本；舊工作不在 dispatch 時偷偷改用新入口。
- 缺入口、入口衝突或 manifest 損壞時，在建立外部作用前明確失敗。
- Function 的預設 `cwd` 必須另定；不能因 leaf 變 directory 就悄悄改變相對路徑意思。

## Definition 範圍

不能把整個 agent root 都 hash 成 Function version：新增一則 message 不應讓 `ask` 的程式版本改變。只 hash `call` 也可能漏掉 helper、prompt 與必要設定。

Directory Function 後續可由 manifest 明列：

```text
Definition inputs  入口、helpers、必要設定
Mutable memory     messages、memory、工作成果
External inputs    環境、shared library、外部服務
```

第一版可先支援明列檔案，不急著自動掃描完整依賴圖。實際版本檢查方式另見 [`FILESYSTEM-MEMORY-CANDIDATES.md`](FILESYSTEM-MEMORY-CANDIDATES.md)。

## 何時才值得拆 child Task

建議至少遇到一項才拆：

- child 結果要獨立保存或讓 parent 重啟後繼續觀察。
- child 有不可重做的外部作用，需要自己的 intent／Receipt。
- 希望在 child 邊界 pause、取消、排隊或限制資源。
- child 本身會被其他 Functions 重用與直接呼叫。

只為了程式碼可讀性，普通函式、library 或同一 process 內部模組已足夠。

## 建議順序

1. 保持 leaf file 是最小形式。
2. 試固定 `directory/call`，證明同一 `aos exec ./ask` 可跨越 file→directory。
3. 再比較 manifest 與薄 launcher；不急著定正式格式。
4. 只有需要安全落盤邊界時才引入 composite child Tasks。
5. 只有需要獨立地盤與生命週期時才升成 child agent。

## 尚待原型

- leaf `ask` 換成 `ask/call` 後，同一命令與 argv／stdin／stdout／stderr／exit 是否保持一致。
- file→directory 前後，預設 `cwd` 與相對檔案是否不變。
- 放入假的 `main.py`、`run` 或其他 executable，AOS 是否仍只使用正式入口。
- 缺入口、雙入口、壞 manifest 是否在任何 effect 前失敗。
- Task 接受後修改入口，舊 Task 是否停止而不跑新版。
- 只整理成 directory 時，Task tree 是否仍只有一個 Task。
- composite 在 child Receipt 已提交後 crash，是否不重做該 child effect。
- 普通 `subs/foo/` 是否不會自動得到 agent 身分。
- 明確登記 child agent 後，它是否有自己的 queue、checkpoint 與寫入權。
- Definition manifest 改 helper 時會換版，改 message 時不會換版。
