# Arxiv Crawler

這是一個簡單且功能強大的單檔 Python 爬蟲工具，旨在自動化從 [arXiv.org](https://arxiv.org/) 搜尋並下載研究論文。它支持關鍵字過濾、時間區段篩選，並可以選擇下載 PDF 檔案或 LaTeX 源碼壓縮包。

## 功能特點

- **多關鍵字搜尋**：支持多個關鍵字組合（使用 `AND` 邏輯）。
- **時間區段過濾**：可指定開始與結束日期。
- **格式選擇**：可選擇下載 `.pdf` 或 `.tar.gz` (LaTeX 源碼)。
- **自動頻率限制**：內置下載間隔（每分鐘最多 5 篇），以符合 arXiv 的存取政策。
- **詳細錯誤回饋**：提供清晰的查詢與下載狀態訊息。
- **無外部依賴**：僅使用 Python 標準庫，無需安裝額外的套件。

## 安裝與需求

- 需求：Python 3.6+
- 不需要安裝任何第三方庫（如 `requests` 或 `beautifulsoup4`）。

## 使用方法

將 `crawler.py` 下載到您的電腦後，即可在終端機執行：

### 1. 基本下載 (PDF)
下載包含 "quantum" 關鍵字的最新 5 篇論文：
```bash
python crawler.py --keywords quantum
```

### 2. 多關鍵字與指定日期
搜尋 2023 年間包含 "machine learning" 和 "transformer" 的論文：
```bash
python crawler.py --keywords "machine learning" transformer --start-date 2023-01-01 --end-date 2023-12-31
```

### 3. 下載 LaTeX 源碼
如果您需要論文的 TeX 原始檔案：
```bash
python crawler.py --keywords "black hole" --format latex --max-results 2
```

### 4. 指定輸出目錄
```bash
python crawler.py --keywords physics --output ./my_research_papers
```

## 參數說明

| 參數 | 說明 | 預設值 |
| :--- | :--- | :--- |
| `--keywords` | (必填) 搜尋關鍵字，多個關鍵字以空格分隔。若關鍵字含空格請加引號。 | 無 |
| `--start-date` | 開始日期，格式為 `YYYY-MM-DD`。 | 無 (從最早開始) |
| `--end-date` | 結束日期，格式為 `YYYY-MM-DD`。 | 無 (直到現在) |
| `--output` | 下載檔案儲存的目錄路徑。 | `./downloads` |
| `--max-results` | 最大下載數量。 | `5` |
| `--format` | 下載格式：`pdf` 或 `latex`。 | `pdf` |

## 注意事項

1. **頻率限制 (Rate Limiting)**：為了遵守 arXiv 的規定，程式在每篇論文下載之間會自動暫停 **12 秒**。請勿隨意修改此間隔以避免您的 IP 被 arXiv 封鎖。
2. **檔名處理**：程式會自動將論文標題轉換為合法的作業系統檔名（移除特殊字元如 `/`, `:`, `*` 等）。
3. **網路連線**：下載過程取決於 arXiv 伺服器的穩定性。若遇到 404 錯誤，通常是因為該文章尚未生成 PDF 或源碼包，程式會跳過並繼續下一篇。

## 免責聲明

本工具僅供學術研究使用。請在使用時遵守 [arXiv API User Manual](https://arxiv.org/help/api/user-manual) 的相關規定。
