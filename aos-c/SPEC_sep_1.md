# 規格:記錄之間加一行空白分隔(八行 → 九行)

狀態:**待實作**。

## 背景:為什麼要加

現在一筆記錄剛好八行、記錄之間沒有分隔符號,所以**少一行或多一行,後面全部錯位**,
而且錯位後每連續八行仍是語法合法的一筆 —— 解析器偵測不到,執行起來就是跑一個沒有
人寫過的指令(詳見 `docs/exec.md`、`docs/format.md` 現有的「⚠ 錯位」兩節)。

加一行「固定為空」的分隔,就讓錯位變得**可偵測**:少行/多行會讓原本該落在分隔位置
的空白行,被非空的資料行頂掉,解析器一看就知道對不齊,直接停。

## 定案的核心決定

- **每筆記錄 = 九行:八行欄位(argv, stdin, stdout, stderr, exit, cwd, env, extra,
  順序與意義完全不變)+ 第九行固定為空的分隔行。**
- 分隔行**永遠是空的**(只有一個 `\n`,CRLF 則是 `\r\n`)。它靠**位置**辨識,不是
  靠內容 —— 讀完八行欄位之後的那一行就是它,所以它是空的不會跟「某個欄位剛好是空」
  混淆。
- 讀到分隔行**不是空的** → 新狀態 `InstState::MissingSeparator`,這是錯位的信號,
  在串流模式下跟其他解析失敗一樣**中止整輪**。
- 一個排好的檔案結尾是 `...extra\n\n`(extra 行 + 空白分隔行)接 EOF。因此下一次
  `read_instruction` 讀第 1 行時立刻 EOF、回 `InstState::Eof`,乾淨結束不受影響。

## 影響面

### 常數與型別(`include/aos/inst.hpp`)

- `kInstLineCount` **維持 8**(它驅動 `lines[8]` 陣列與八次欄位讀取迴圈,代表的是
  **欄位行數**)。更新它的註解:一筆記錄是這八行欄位再加一行空白分隔,共九行。
- `InstState` 尾端**新增** `MissingSeparator`(附上「讀取:八行欄位之後那一行不是
  空的,代表記錄錯位」的註解)。只能加在最後,不動既有值。
- 檔頂那段檔案級註解(現在寫「An instruction is eight lines」)更新成九行:八行
  欄位 + 一行空白分隔,並說明分隔行的用途是讓錯位可偵測。`read_instruction` /
  `write_instruction` 的 doc 也順帶更新(「eight lines」→ 九行)。

### 讀取(`src/inst_format.cpp` 的 `read_instruction`)

八行欄位的讀取迴圈**完全不動**。在迴圈之後、`split_argv` 之前,加分隔行的處理:

```cpp
/* 第 9 行:固定為空的記錄分隔。它讓錯位可被偵測 —— 少行/多行會讓非空的
 * 資料行落在這個位置。它靠位置辨識,所以是空的不會跟空欄位混淆。 */
std::string separator;
switch (read_line(in, separator, max_record_bytes - used)) {
case LineState::Ok:
    if (!separator.empty()) {
        return fail(InstState::MissingSeparator);
    }
    break;
case LineState::Eof:        /* 八行讀完卻沒有分隔行 = 記錄被截斷 */
case LineState::Incomplete:
    return fail(InstState::Incomplete);
case LineState::TooLong:
    return fail(InstState::TooLong);
case LineState::ReadError:
    return fail(InstState::ReadError);
}
```

- 分隔行的位元組**不計入** `used`(位元組預算算的是八行欄位,分隔行本該是空的)。
- 順序:欄位迴圈 → 分隔行檢查 → `split_argv` → `split_env`。分隔行檢查放在
  split 之前,錯位優先報成 `MissingSeparator`(比讓它變成某個欄位的間接錯誤清楚)。

### 寫入(`src/inst_format.cpp` 的 `write_instruction`)

八行欄位的輸出不動,最後補一行空白分隔:

```cpp
out << inst.extra << '\n';   /* 第 8 行 */
out << '\n';                 /* 第 9 行:固定為空的記錄分隔 */
```

### 狀態字串(`src/inst.cpp` 的 `to_string(InstState)`)

`MissingSeparator` → 加一個訊息,例如
`"record is not terminated by a blank separator line"`。

### C ABI(`include/aos/aos.h` + `src/capi.cpp`)

- `aos.h` 的 `aos_inst_state` 列舉尾端**新增** `AOS_INST_MISSING_SEPARATOR`
  (append-only,soname 不動),附註解。
- `capi.cpp`:
  - `AOS_INST_MISSING_SEPARATOR` 與 `InstState::MissingSeparator` 的
    `static_assert` 配對(比照其他狀態,加在對照清單尾端)。
  - C↔C++ 狀態轉換的 switch / 對照表補這一項。
  - C 版 `to_string`(若有獨立實作)補這一項。
- `aos.h` 檔頂描述格式的註解(現在寫「eight lines」)更新成九行。

### 測試

- `tests/test_inst_write.cpp`:round-trip 與逐位元組的期望輸出,每筆尾端多一個
  `\n`。凡是手寫「預期序列化字串」的地方都要補分隔行。
- `tests/test_inst_read.cpp`:所有手寫的輸入記錄(尤其連續兩筆的
  `test_two_records_reuse_instruction`、CRLF 那幾個)都要在每筆後面加一行空白。
  round-trip 測試因為讀寫對稱,只要輸入資料更新即可。
- `tests/test_inst_read_errors.cpp`:
  - 既有「不足八行 → Incomplete」的案例,確認在九行世界下仍成立(八行 + EOF →
    `Incomplete`,因為缺分隔行)。
  - **新增**:八行欄位齊全、但第九行不是空的(例如直接接下一筆的 argv)→
    `MissingSeparator`。
  - **新增**:一個少一行的檔案(兩筆、第一筆只有七行欄位)→ 讀第一筆時,原本的
    分隔行位置被下一筆的資料頂掉 → `MissingSeparator`(或更早的
    `EnvEntryMalformed`,兩者都可接受,擇一斷言並註明)。
- `tests/test_capi.c`:同樣所有手寫記錄補分隔行;若有列舉對照或 `to_string`
  的覆蓋測試,補 `AOS_INST_MISSING_SEPARATOR`。
- `tests/test_inst_limits.cpp`:若用到手寫記錄,一併補分隔行。

> 注意:很多測試用 `printf`/字面字串拼記錄。**逐一檢查每個手寫記錄都補上第九行**,
> 這是這次最容易漏、也最容易讓測試紅的地方。

## `.md` 文件與程式本體的說明(由主 agent 收尾,subagent 不要動 `.md`)

`README.md`(`printf` 範例每筆多一個 `\n`、八行 → 九行的敘述)、`docs/format.md`
(「一筆接一筆」「⚠ 錯位」「為什麼是這個格式」「狀態」等節大改 —— 錯位現在**測得
到**了)、`docs/exec.md`(「⚠ 錯位是偵測不到的」一節改寫)、`docs/architecture.md`
(「the format has no record separator」那段理由要翻新)、`docs/capi.md`、
`docs/cxxapi.md`(狀態表、行數)。這些主 agent 自己改。

## 驗證

- WSL Ubuntu:`make strict` 全綠、零警告。
  `wsl -d Ubuntu -e bash -lc 'cd /mnt/c/code/mine/simple_tools/aos-c && make strict'`。
- 完成後回報 `src/inst_format.cpp` 的大小。它目前已約 9.3KB(超過 8KB 的軟性訊號),
  這次會再長一點。依專案規則(大小是訊號不是硬上限)**不要擅自拆檔**,只回報數字;
  真的明顯超標再由主 agent 決定。
