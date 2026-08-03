# testing — 跑測試（單檔工作流）

← [WORKFLOWS](../WORKFLOWS.md)｜[INDEX](../INDEX.md)

本檔是「單檔工作流」：一個 `.md` 同時是入口與內容。膨脹了就照 [DEV-GUIDE](../DEV-GUIDE.md) 升級成資料夾型。

dcap **無單元測試框架、無 lint**；驗證＝先 build、再跑端對端 smoke test。

## 指令

- **建置**：`mingw32-make`（Windows；UNIX 為 `make`）。本機無 cmake、無 `make`，只有 `mingw32-make`。
- **端對端驗證（Claude 自己跑、鐵律要求的那套）**：
  ```
  dcap new demo && cd demo && dcap build
  ```
  預期：成功建置並執行後**印出 Hello World**。這就是完整驗證。

## 測試分類

- **本機可跑**：build（`mingw32-make`）＋上面的端對端 smoke test，Claude 自己跑。
- **跨平台差異**（Windows/MinGW vs. UNIX 的 `#ifdef _WIN32` 分支）：本機只能驗當前平台；另一平台的實機驗證跑不了時 → 記 [WAIT_USER](../WAIT_USER.md) 交使用者在該平台驗。
