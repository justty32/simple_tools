# 文件索引

`docs/` 放跨 package 的研究與架構報告。實際 API、執行方式與局部設計仍以程式旁的
README／docstring 為準。

## 架構主線

| 文件 | 性質 | 用途 |
|---|---|---|
| [Agent World](agent-world/README.md) | 設計底圖 | path、namespace、memory、Turn／Round 與安全邊界 |
| [Agent framework 調查](agent-framework-review/README.md) | 參考研究 | 比較七套框架並擷取可採用的設計切片 |
| [Sandboxing](sandboxing/README.md) | 安全研究 | 現有邊界、容器隔離與落地建議 |

## 跨語言固化

這兩份是替代方案評估，不表示已決定重寫：

| 文件 | 結論摘要 |
|---|---|
| [C／C++ 固化](cpp-solidification/README.md) | C++ 適合穩定語意核心與 native adapter；目前不宜整套重寫 |
| [Lisp／Janet 固化](lisp-solidification/README.md) | Janet 適合穩定的資料與 reduction 核心；OS／provider glue 留在 adapter |

## 歷史材料

- [`archive/cpp-streaming-client-notes.md`](archive/cpp-streaming-client-notes.md)：
  2026-08-05 的 C++ HTTP／SSE client 草稿；僅供追溯，不代表目前實作。

文件角色、更新優先序與註解準則見 [`DOCUMENTATION.md`](DOCUMENTATION.md)。
