# spec 格式 0.1.0：外殼

一份 spec = **一個 OpenAI tool JSON**，外加一個 `_extra`，講怎麼把它變成實際的執行。

這份是**契約**，不是某個實作的說明 —— 以後別的語言的 lib 讀同一份 JSON，要做出
一模一樣的事，所以每條規則都是死的，實作不要自行加碼。

這裡只講外殼（所有 `_type` 共通的部分）。`_type: "exec"` 的那套在 [EXEC.md](EXEC.md)。

```json
{
  "type": "function",
  "function": {
    "name": "resize",
    "description": "把圖片縮到指定寬度，蓋掉原檔",
    "parameters": {
      "type": "object",
      "properties": {"path": {"type": "string"}, "width": {"type": "integer"}},
      "required": ["path"]
    }
  },
  "_extra": {
    "_version": "0.1.0",
    "_type": "exec",
    "…": "其餘的鍵由 _type 決定，見 EXEC.md"
  }
}
```

## 前半：原封不動的 OpenAI tool

`type` 和 `function` 兩個鍵就是 OpenAI function calling 那個 tool 物件，一個字都沒改。
好處是它跟 `llms.func_schema` 從 python 函式生出來的東西是同一種形狀，兩個來源匯得起來。

**`_extra` 送不出去。** 多掛一個未知的鍵，OpenAI、LiteLLM、LM Studio 三邊各有各的
嫌法。所以進 `tools=` 之前一定要剝掉，剝完剩下的就是乾淨的 schema。

## 後半：`_extra`

只有兩個 `_` 開頭的**保留鍵**。標準庫先讀這兩個，其餘的鍵一律交給 `_type` 指定的
那套去解析：

| 鍵 | 是什麼 |
|---|---|
| `_version` | 這份 `_extra` 的規範版本 |
| `_type` | 用哪種方式執行 |

兩個都**沒有預設值**，缺了或不認得就是壞檔 —— 猜一種執行方式，比乾脆讀不懂危險得多。

判準很簡單：**所有 `_type` 都成立的東西才配當保留鍵**，只有某一種成立的就是普通鍵。
所以連 `timeout`、`limits` 這種看起來很通用的也不是保留鍵，它們歸各自的 `_type` 管。

### `_type`

| 值 | 意思 | 規範 |
|---|---|---|
| `"exec"` | 跑一個 linux 檔案，argv + stdin/out/err | [EXEC.md](EXEC.md) |

之後會有 python import、C++ include、HTTP API，各自是一份平行的文件，
`_extra` 底下的鍵也各自換一套。

### `_version`

semver 字串。版本規則等第一版定稿再寫死；在那之前，**`_version` 不完全相符就直接
拒絕**，不猜 —— 0.x 期間 semver 本來就允許 minor 破壞相容性。

大致的方向是：major 不同就拒絕，minor 比自己新則照讀、忽略不認得的鍵（所以 minor
升版只准加東西），patch 是純文字澄清。

## 一個檔案可以裝好幾個

最外層可以是一個 object，也可以是一個 **array**：

```json
[
  {"type": "function", "function": {"name": "resize"}, "_extra": {…}},
  {"type": "function", "function": {"name": "crop"},   "_extra": {…}}
]
```

相關的工具擺同一份比較好維護，而且 array 裡的每一項各自有自己的 `_type` ——
同一個檔案裡混著跑執行檔的和打 HTTP 的完全合法。

同一個檔案裡 `function.name` 撞名是**錯**，不是先到先贏：那明顯是打錯字，
安靜挑一個只會讓人找不到另一個。跨檔案撞名則比照 `PATH`，先掃到的贏。

## 存在哪裡

慣例是放在工具旁邊的 `.specs/` 底下，工具跟它的描述一起搬、一起進 git，
一個資料夾自我完備。

**檔名沒有意義** —— 讀的是整個 `.specs/*.json`，一份 .json 描述哪些東西、叫什麼名字，
完全由內容決定。單一工具慣例上叫 `<執行檔名>.json`，但那只是好找，不是規則。

**`_extra` 裡所有的相對路徑，中心都是這份 .json 自己的位置**，不是工具目錄、也不是
呼叫端的 cwd。載入時就翻成絕對路徑。（唯一的例外在 EXEC.md：`exec[0]` 不含斜線時
會去查 `$PATH`。）

## 沒有註解

JSON 沒有註解語法，所以說明都在這幾份 .md 裡，`.json` 本身只放資料。
