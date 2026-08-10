# freepy 的 Lisp／Janet 固化評估

日期：2026-08-10

## 結論

可行，而且很適合採用「Python 先做 demo，Janet 再固化」；但固化的對象應是**穩定語意核心**，不是把所有 Python 與 OS glue 逐行翻譯。

依目前程式與規劃粗估，約 **60–75% 的核心語意**適合 Janet：資料契約、schema、路徑與 namespace、狀態機、tool loop、權限縮減、記憶引用、context manifest 都很合適。這是架構占比，不是 LOC 精算。

不應硬塞進 Janet 核心的部分包括：provider SDK、HTTPS/SSE、Python callable 自省、Podman/cgroup/seccomp、FUSE/9P mount，以及尚未證明可攜的 Windows subprocess。這些應透過 typed adapter、local proxy、stdio 或 OS service 接入。

## 一句話架構

```text
Python demo / research oracle
          │ frozen JSON fixtures + traces
          ▼
Janet semantic core ── typed intents ──► trusted effect adapters
 identity / policy / state               HTTP proxy / process / container / FUSE
 tool protocol / memory / context
```

Lisp 固化的價值不只是語法短，而是讓系統以少量、可組合、可檢查的資料與 reduction 規則表達。Linux／Plan 9 的 namespace、mount、handle、process，正好可映射成 environment、binding、capability reference、evaluator；但隱喻不能取代 OS enforcement。

## 最先做什麼

第一個跨語言切片應是 `tooljson`：

1. 凍結 `FORMAT 0.1.0` 的代表性 fixtures。
2. Janet 實作 parser、validator、registry 與 `exec` argv builder。
3. Python 與 Janet 跑同一組 golden vectors。
4. Janet 先以 shadow／CLI backend 接入，不立刻刪 Python。

這能直接驗證「spec 才是產品、Python 只是第一個實作」是否成立，也會逼出 JSON、路徑、錯誤字串、排序、timeout 與 subprocess 的真實跨語言差異。

## 導覽

- [評估方法與判準](01-method.md)
- [現有 freepy 模組](02-current.md)
- [IDEAS／PLAN 中的規劃](03-planned.md)
- [langlab-janet 實證](04-janet-evidence.md)
- [Python demo → Janet 固化流程](05-process.md)
- [建議架構與語言邊界](06-architecture.md)
- [分期路線與驗收](07-roadmap.md)
- [風險、反證與待決策](08-risks.md)
- [HTML 導覽](index.html)

## 本次實跑摘要

- freepy `agentloop`：35 個離線關卡全過。
- freepy `modelcards`：31/31 過。
- freepy `base_tools`：檔案與 containment 過；6 個 POSIX shell 關卡在 Windows 明確失敗。
- freepy `tooljson`：32/45 過；13 個 exec 關卡在 Windows 因測試腳本不是 Win32 executable 失敗。
- langlab-janet：basic 與 6 個 `llm-http` 測試通過；3 個 `pi-shell` 測試在 Windows 以 access violation 結束。

因此報告把 Janet subprocess 的可攜性列為未過關，而不是因「Janet 有 `os/spawn`」便宣稱完成。

## 範圍

本報告唯讀評估 `freepy` 現有程式、`IDEAS.md`、`ROADMAP.md` 與各 `PLAN.md`，並參考實際存在的 `C:\code\mine\langlab-janet`。使用者提到的 `C:\code\langlab-janet` 不存在；報告採用前者。
