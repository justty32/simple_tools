# simple_tools

這個 repository 收納兩個可獨立使用的專案，以及圍繞 agent runtime 的設計研究：

| 區域 | 狀態 | 內容 |
|---|---|---|
| [`freepy/`](freepy/README.md) | 實作中 | Python 的 LLM、工具與 agent loop；另含多 agent／runtime 規劃 |
| [`dcap/`](dcap/README.md) | 可用 | 以 C++23 實作的 C／C++ 專案產生器 |
| [`docs/`](docs/README.md) | 研究資料 | agent framework、agent world、sandbox 與跨語言固化評估 |

## 從哪裡開始

- 想使用或開發 FreePy：先讀 [`freepy/README.md`](freepy/README.md)。
- 想建立 C／C++ 專案：先讀 [`dcap/README.md`](dcap/README.md)。
- 想理解整體架構與研究結論：從 [`docs/README.md`](docs/README.md) 選主題。
- 想知道筆記、規格、README 與程式註解各自應放什麼：見
  [`docs/DOCUMENTATION.md`](docs/DOCUMENTATION.md)。

根目錄只保留跨專案入口；元件用法放在元件旁，跨元件研究放在 `docs/`，已結案但仍有
追溯價值的材料放在 `docs/archive/`。
