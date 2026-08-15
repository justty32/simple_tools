# AOS（原 Agent Machine）

AOS 是位於 Linux 與上層工作之間、以 Function 與 filesystem memory 為核心的執行與恢復系統。

準備親手寫 C++，請先讀 [`START-HERE.md`](START-HERE.md)。它整理目前證據、第一個 C++ 落點、閱讀順序與重跑命令。

## 目錄地圖

| 入口 | 內容 | 地位 |
|---|---|---|
| [`START-HERE.md`](START-HERE.md) | 現況與 C++ 實作交接 | 現行開工入口 |
| [`full/`](full/README.md) | AOS 完整設計 `00`–`13` | 長期主線設計，不等於全部已實作 |
| [`full/options/`](full/options/README.md) | 尚未裁決的方案與推翻條件 | 不可當成承諾或 ABI |
| [`workbench/`](workbench/README.md) | P0／P1 原型、測試與故障證據 | 可執行參考，非正式規格 |
| [`wait_user/`](wait_user/README.md) | `full/` 與 options 的繁中濃縮導讀 | 幫助閱讀，不另立真源 |
| [`archived/`](archived/README.md) | 被取代的舊設計 | 只供追溯 |

## 權威順序

1. 使用者較新的明確決定。
2. 實際程式、測試與相鄰工作台 README 所證明的行為。
3. [`full/00-STATUS-AND-SOURCES.md`](full/00-STATUS-AND-SOURCES.md) 與 `full/` 主題頁。
4. `full/options/` 的未決方案。
5. `workbench/` 的原型格式與研究過程。
6. `archived/` 的歷史內容。

P1a-2 Phase 2 的本輪窄切片已結束，Python 工作台目前是 74/74 tests，其中 16 項直接涵蓋 process 路徑；這不代表完整 AOS、正式 C++ Runtime、scheduler 或 Agent 已完成。

## 舊 `agent_machine` prototype

[`agent_machine/`](agent_machine/) 與根層 executable `agent-machine` 是較早的離線 Python prototype。它使用舊 Function／Step 詞義，只提供狹窄的 echo 與 run-state 操作，沒有目前 P1a-2 的 process evidence、crash recovery 或 Task tree 契約。

除非要維護歷史行為，新的 AOS／C++ 實作不要從這個 package 推導架構或格式。舊檢查命令仍是：

```sh
cd agent-machine
python3 -m agent_machine._checks
```

2026-08-13 的舊根層設計已逐 byte 封存在 [`archived/2026-08-13-snapshot/`](archived/2026-08-13-snapshot/README.md)，不得覆蓋現行設計與測試證據。
