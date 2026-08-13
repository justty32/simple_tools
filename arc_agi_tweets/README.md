# ARC-AGI Tweets

這是一個獨立的 X（Twitter）推文蒐集器，目的不是把所有提到 ARC 的雜訊都存下來，
而是留下較值得讀的 ARC-AGI 研究、實驗、論文、程式碼與技術討論。

預設會追蹤：

- 全站的 `ARC-AGI`、`ARC-AGI-2/3`、`Abstraction and Reasoning Corpus`；
- François Chollet（`@fchollet`）；
- Richard Sutton（`@RichardSSutton`）；
- 需求中寫的 `@wolfrein`；這個拼法目前無法可靠確認，handle 不同時請改 `config.toml`；
- `@arcprize`、Greg Kamradt 與 Mike Knoop 等 ARC Prize 第一手來源。

工具使用官方 X API v2，避免依賴容易失效的網頁 selector 或非官方 guest token。它會跨次
去重並使用 `since_id`，減少重複讀取與費用。API 回傳資料會存到 `data/tweets.jsonl`，
打分後的精選則寫到 `data/curated.md`；`data/` 已被 git 忽略。所有 source 與分頁皆依序
請求，程式不會同時打開多條網路連線。

## 開始使用

需要 Python 3.11 以上；專案沒有第三方 Python 依賴。

1. 到 X Developer Console 建立 App，取得 Bearer Token，並確認帳號內已有足夠 credits。
2. 在 PowerShell 設定 token（只存在目前 shell，不會寫入 repository）：

   ```powershell
   $env:X_BEARER_TOKEN = "你的 Bearer Token"
   ```

3. 先預覽 query 與最高費用：

   ```powershell
   cd C:\code\mine\simple_tools\arc_agi_tweets
   python -m arc_tweets plan
   ```

4. 確認後抓取最近 7 天：

   ```powershell
   python -m arc_tweets fetch --execute
   ```

刻意要求 `--execute` 是因為 X API 讀取會計費。依 2026-08-13 的官方公開價格，Post read
為 US$0.005／筆；價格可能改變，執行前仍應以 Developer Console 為準。預設三個 source、
每個 25 筆、各一頁，單次最壞情況約 75 筆／US$0.375。

## 常用操作

只抓少量並提高精選門檻：

```powershell
python -m arc_tweets fetch --page-size 10 --min-score 10 --execute
```

搜尋指定歷史區間（完整歷史 endpoint 的權限與費用依 X 方案而定）：

```powershell
python -m arc_tweets plan --archive --page-size 100 --pages 2
python -m arc_tweets fetch --archive `
  --start-time 2025-01-01T00:00:00Z `
  --end-time 2025-02-01T00:00:00Z `
  --page-size 100 --pages 2 --execute
```

換設定檔或輸出位置：

```powershell
python -m arc_tweets --config config.local.toml fetch --output-dir data-test --execute
```

`config.local.toml` 已被 git 忽略，適合放自己的查詢；請勿把 token 放進 TOML。

## 「有料」怎麼判斷

每篇推文會得到可解釋的分數，主要看：

- 是否直接提到 ARC-AGI，以及是否包含 abstraction、generalization、program synthesis、
  test-time、world model、contamination 等技術訊號；
- 是否附 arXiv、GitHub、OpenReview、ARC Prize、Kaggle 等一手材料；
- 是否包含實驗、結果、ablation、資料集、原始碼等可驗證內容；
- 文字是否有足夠說明、是否由指定／可信作者發表，以及互動討論量；
- 對 `AGI achieved`、`game over`、giveaway 等誇張或宣傳訊號扣分。

互動量只是小幅加分，無法讓空泛爆文壓過有論文或程式碼的技術推文。原始資料不會因低分
被刪掉，只有 `curated.md` 會套用 `min_score`。

## 排程

近期搜尋只涵蓋最近 7 天，建議每天跑一次。Windows 工作排程器可把動作設成：

```text
Program:    C:\path\to\python.exe
Arguments:  -m arc_tweets fetch --execute
Start in:   C:\code\mine\simple_tools\arc_agi_tweets
```

Bearer Token 應透過執行排程的使用者環境或安全的 secret mechanism 提供，不要直接寫在
Arguments、程式碼或版本控制中。

官方參考：[Recent Search quickstart](https://docs.x.com/x-api/posts/search/quickstart/recent-search)、
[Search Posts](https://docs.x.com/x-api/posts/search/introduction)、
[X API pricing](https://docs.x.com/x-api/getting-started/pricing)。

## 測試

測試完全離線，不會呼叫 X，也不會產生費用：

```powershell
python -m unittest discover -s tests -v
```
