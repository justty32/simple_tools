# simple_tools

這個 repository 收納兩個可獨立使用的專案，以及圍繞 agent runtime 的設計研究：

| 區域 | 狀態 | 內容 |
|---|---|---|
| [`freepy/`](freepy/README.md) | 實作中 | Python 的 LLM、工具、agent loop、adapters 與 parked prototypes；未來規格集中在 docs |
| [`dcap/`](dcap/README.md) | 可用 | 以 C++23 實作的 C／C++ 專案產生器 |
| [`docs/`](docs/index.html) | 研究資料 | agent framework、agent world、sandbox 與跨語言固化評估 |

## 從哪裡開始

- 想使用或開發 FreePy：先讀 [`freepy/README.md`](freepy/README.md)。
- 想建立 C／C++ 專案：先讀 [`dcap/README.md`](dcap/README.md)。
- 想理解整體架構與研究結論：從 [`docs/index.html`](docs/index.html) 的離線導覽選主題；純文字索引仍在 [`docs/README.md`](docs/README.md)。
- 想知道筆記、規格、README 與程式註解各自應放什麼，以及單檔多大就該拆：見
  [`docs/CONVENTIONS.md`](docs/CONVENTIONS.md)。

## 一鍵驗證

```sh
./smoke.sh          # 離線 checks、shell executables、dcap C/C++ 產生與建置
./smoke.sh --live   # 再驗 DeepSeek 與 LM Studio 的所有家用模型 alias
```

`--live` 要求目前 shell 已設定 `DEEPSEEK_API_KEY`，且 LM Studio 已在
`localhost:1234` 提供 API。若 port 4000 沒有 proxy，腳本會暫時啟動一個並在結束時關閉；
若已有 proxy 則直接沿用，不會將它關掉。公司網路上的遠端 Ollama 不屬於家用必過項目。

根目錄只保留跨專案入口；元件用法放在元件旁，跨元件研究放在 `docs/`，已結案但仍有
追溯價值的材料放在 `docs/archive/`。
