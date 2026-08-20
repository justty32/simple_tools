# 規格 2:env 從「替換」改成「純擴充」

狀態:**待實作**。承接 [SPEC_env_1](SPEC_env_1.md)(env 從檔案路徑改成 inline
KEY=VALUE 清單)。這一份只動**執行語意**,不動格式:第 7 行仍然是 tab 分隔的
`KEY=VALUE`,讀寫規則、上限、`EnvEntryMalformed` 全部不變。

## 定案的核心決定

- **非空 env = 在繼承的環境上擴充**,不再是整組替換。語意等同對每一筆
  `KEY=VALUE` 呼叫 `setenv(KEY, VALUE, overwrite=1)`:同名的**覆寫**,其餘的
  **新增**,沒被提到的父環境變數**保留**。
- **空 env = 純繼承,不動任何東西**(與原本相同)。
- **不保留「完全替換」的能力**。使用者已確認純擴充就好;「清空後只用這些」這件事
  這個格式不再能表達。這是刻意的取捨,不是疏漏 —— 要乾淨環境的人自己在指令裡用
  `env -i cmd ...` 之類的方式達成。
- **重複的鍵:後者贏**。因為是照順序逐筆 `setenv` overwrite,`DUP=first` 之後
  `DUP=second`,最後 `DUP=second`。這跟舊的「整組塞進 environ、由 libc 決定誰贏
  (通常是前者)」不同,是這次改動的自然結果,要一併更新文件與測試。

## 為什麼用 `setenv` 而不是合併成 `char**` 再設 `environ`

`setenv` 就是「擴充」的原生原語:覆寫同名、新增其餘、保留其他,一行一筆掃過去就
對了。自己合併 `environ` 與 `inst.env` 成一個新的 `char**` 還要處理去重與同名覆寫,
是把 libc 已經做好的事重寫一遍。在 `fork` 之後、`execvp` 之前的子行程裡呼叫
`setenv` 是安全的(子行程單執行緒),而且 `setenv` 會自己複製字串,沒有生命期問題。

## 影響面

### 執行(`src/exec.cpp`)——會變乾淨

- `run_child` 裡把原本的
  ```cpp
  if (replace_env) {
      environ = envp.data();
  }
  ```
  換成逐筆 `setenv`:
  ```cpp
  /* env 非空就在繼承的環境上逐筆覆寫/新增(setenv 語意),不是整組替換。
   * entry 在讀寫時已驗證為合法 KEY=VALUE,所以 '=' 必然存在且不在最前面。 */
  for (const std::string &entry : inst.env) {
      const std::string::size_type eq = entry.find('=');
      if (setenv(entry.substr(0, eq).c_str(), entry.c_str() + eq + 1, 1) != 0) {
          _exit(kExitSetupFailed);   /* 通常是 ENOMEM,歸類為佈置失敗(126) */
      }
  }
  ```
- 連帶移除:`execute` 裡的 `replace_env`、`to_c_envp(inst)` 這一行、以及檔案頂端的
  `extern char **environ;`。`to_c_argv` 保留不動。
- 參數方向(`inst_t &`)不變。`ExecState` **不新增狀態**:`setenv` 失敗沿用既有的
  佈置失敗路徑(`_exit(126)`),跟重導向、chdir 失敗一致。

### 移除 `to_c_envp`(公開 C++ helper)

擴充之後 exec 不再需要它,而它建的是「替換」語意的 envp(空 = 只有結尾 null =
空環境),留著會跟新語意矛盾、誤導呼叫端。C++ 介面沒有跨版本承諾,移除是乾淨的:

- `include/aos/inst.hpp`:移除 `to_c_envp` 宣告與那段註解。
- `src/inst.cpp`:移除 `to_c_envp` 定義。
- `tests/test_inst_read.cpp`:移除 `test_to_c_envp()` 整個函式與 `run_*` 裡的呼叫。
- `to_c_argv` 全部保留。

### C ABI(`src/capi.cpp` + `include/aos/aos.h`)

- **不動任何簽章、不加不減任何列舉值**。env 的 setter/getter
  (`aos_instruction_push_env` / `_env` / `_env_count`)語意不變 —— 它們只是把
  `KEY=VALUE` 放進清單,怎麼套用是 execute 的事。
- 只需更新 `aos.h` 描述 env 語意的註解(見下)。soname 不動。

### 註解/文件字串(在程式碼檔內,這次一併改)

- `include/aos/inst.hpp`:`inst_t::env` 那段註解把「非空則是整組替換,不是擴充」
  改成「非空則在繼承的環境上擴充(覆寫同名、新增其餘、保留其他);空 = 純繼承」。
- `include/aos/exec.hpp`:`execute` doc 裡 `env` 那幾行(替換 → 擴充),並把
  「passed as it stands / replace」的敘述改成擴充與「後者贏」。
- `include/aos/aos.h`:第 163-164 行「any entry at all replaces it wholesale」改成
  擴充;第 272-273 行同 exec.hpp。

### 測試(`tests/test_exec.cpp`)

- `test_env_replaces_environment` → 改名 `test_env_extends_environment`,並改判定:
  - 父行程設 `AOS_C_TEST_PARENT_ONLY=leaked`。
  - `inst.env = { "MYVAR=hello" }`,指令 `sh -c 'echo $MYVAR:$AOS_C_TEST_PARENT_ONLY'`。
  - 期望輸出 **`hello:leaked\n`**(擴充:父變數仍在,新變數也在)。
- 新增 `test_env_overrides_inherited`:父設 `AOS_C_TEST_OVERRIDE=old`,
  `inst.env = { "AOS_C_TEST_OVERRIDE=new" }`,`echo $AOS_C_TEST_OVERRIDE`,期望
  **`new\n`**(同名覆寫)。
- `test_empty_env_inherits_environment`:不變。
- `test_env_is_passed_through_verbatim`(重複鍵):現在是確定的「後者贏」,把判定從
  「`first` 或 `second`」收緊成 **`second\n`**,並更新註解(逐筆 setenv,後者覆寫
  前者)。記得同步 `run_exec_tests()` 裡的函式名。

## `.md` 文件(由主 agent 收尾,不在這個 subagent 的範圍)

`docs/exec.md` env 一節、`docs/architecture.md` 的 env 表格列與「replace 才是無法
由另一個組出來的」那句理由、`docs/capi.md`、`docs/cxxapi.md`(含移除 `to_c_envp`
一節)、`README.md`。這些主 agent 自己改,subagent 不要動 `.md`。

## 驗證

- WSL Ubuntu:`make strict`(= `-Werror` 全重建 + 跑完整測試),要全綠、零警告。
  指令:`wsl -d Ubuntu -e bash -lc 'cd /mnt/c/code/mine/simple_tools/aos-c && make strict'`。
- 完成後回報 `src/exec.cpp`、`src/inst.cpp` 的大小(bytes)。移除 `to_c_envp` 與
  `environ` 會讓兩檔各縮一點,不會有超標問題。
