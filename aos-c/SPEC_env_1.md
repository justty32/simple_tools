# 規格 1:env 從「檔案路徑」改成 inline 的 KEY=VALUE 清單

狀態:**討論中,尚未實作**。這份記錄已定案的決定與待確認的判斷點,
供實作時當依據。

## 背景與定案的核心決定

- env 過去是 `inst_t.env_path`(一個字串,指向一個 `KEY=VALUE` 每行一筆的
  檔案),exec 時才讀檔。現在改成 **inline**:記錄本身就帶著 env。
- **存放型別:`std::vector<std::string>`**,每個元素是一整串 `"KEY=VALUE"`。
  不是 `map`,也不是 `vector<char*>`。
  - 理由:aos-c 對 env 的本分是「照收整組、原樣替換、傳給子程式」,不查不
    改。查/改單一變數是**產生端**的事(產生端用 map,攤平成幾行 KEY=VALUE
    再交給 aos-c)。所以 aos-c 這邊零查詢碼,vector 最貼近格式與 envp。
  - `char**`(`environ` 要的形狀)永遠只是 exec 前臨時攤出來的東西,跟
    `argv` 存 `vector<string>`、跑前才 `to_c_argv` 成 `vector<char*>` 對稱。
  - C99 重寫時 vector<string> → `char*` 陣列,零痛;map 得自己刻關聯陣列。
- 「未來變數超級多」不成問題:env 給一個有界上限(比照 argv 的 256),
  線性掃描是奈秒級,對上 execvp+子程式的毫秒級是雜訊;何況查改在產生端。

## 影響面

### 資料模型(`include/aos/inst.hpp`)
- `std::string env_path;` → `std::vector<std::string> env;`
- 新增 `constexpr std::size_t kInstEnvMax = 256;`(比照 `kInstArgvMax`)。
- `clear()` 加 `env.clear()`。`extra` 仍為第 8 行,格式維持 8 行。

### 格式與讀寫(`src/inst.cpp`)
- Line 7 從「一個路徑」變成「tab 分隔的 `KEY=VALUE`」,編碼同 argv 行。
- **空的 line 7 = 空 env = 繼承環境**。這是與 argv 的唯一差別:argv 空行是
  錯(EmptyArgv),env 空行合法(0 筆)。
- Reader:切 tab → 每個 token 驗證為合法 `KEY=VALUE`。
- Writer:vector 用 tab 接回,寫前先驗證。

### 執行(`src/exec.cpp`)——會變短
- 砍掉整個 `load_env_file`(不再讀檔)。
- `execute` 直接從 `inst.env` 攤成 `char**`(新增 `to_c_envp`,對稱於
  `to_c_argv`),`replace_env = !inst.env.empty()`,語意不變(空=繼承,
  非空=整組替換)。

### C ABI(`src/capi.cpp` + 公開 C 標頭)
- 從欄位列舉移除 `AOS_FIELD_ENV`(env 不再是字串欄位)。
- 新增比照 argv 的三個函式:
  - `size_t aos_instruction_env_count(const aos_instruction *)`
  - `const char *aos_instruction_env(const aos_instruction *, size_t index)`
  - `aos_inst_state aos_instruction_push_env(aos_instruction *, const char *entry)`

### 測試 + docs
- 所有用到 env_path 的測試(exec、inst 讀寫、capi)改成新的 env 清單與格式。
- 新增測試:round-trip、空 env(繼承)、多筆、格式錯誤、超量、exec 替換環境。
- docs(architecture / cxxapi / README / C 標頭註解)同步:env_path → env、
  移除 EnvFileFailed、描述 line 7 的 inline 格式。

## 待確認的判斷點(各附建議)

1. **錯誤粒度**:只加一個 `InstState::EnvEntryMalformed`(涵蓋「沒有 `=`」
   「key 空」「含 tab/換行」)+ 一個 `TooManyEnv`,而不像 argv 拆成多個。
   兩者附加在列舉尾端,不動既有值。*建議:合併,因 env 是可信的機器輸入。*

2. **C ABI setter 形狀**:`push_env(entry)` 收整串 `"KEY=VALUE"`,而非
   `push_env(key, value)`。*建議:entry,最貼近 vector<string>,產生端自組。*

3. **移除 `ExecState::EnvFileFailed`**:它已完全無法觸發(不再讀檔)。連帶移
   除 C 標頭的 `AOS_EXEC_ENV_FILE_FAILED`、`AOS_CHECK_STATE`、`to_string`。
   代價:ExecState 後續值重新編號 = ABI 破壞,但原型階段可接受,且
   `static_assert` 會自動抓出對不齊。*建議:移除。*

4. **env 上限**:`kInstEnvMax = 256`,比照 argv。*建議:加。*

## 驗證
- WSL Ubuntu:`make clean && make test`,全綠、零警告。
- 注意 `src/inst.cpp` 已接近 8KB 上限;env 解析要寫得緊,exec.cpp 砍掉
  load_env_file 會騰出空間,但兩檔分屬不同檔。若 inst.cpp 明顯超標,先回報
  再決定,別逕自拆檔。
