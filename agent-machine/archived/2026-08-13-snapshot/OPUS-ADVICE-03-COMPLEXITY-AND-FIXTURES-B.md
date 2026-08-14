<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-02-COMPLEXITY-AND-FIXTURES-A.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-04-VISION-AND-ONBOARDING.md)
<!-- archive-nav:end -->

<!-- archive-original:start -->
### 3.2.1 而且不是所有東西都需要一致

現在的文件對所有東西一視同仁地要求 byte-identical，這是規格膨脹的主因。實際上分三層：

| 層 | 內容 | 要求 |
|---|---|---|
| A | 落盤格式、tool result 字串、argv 組裝、stdout／exit code | **byte-identical**，進 fixtures |
| B | `status`／`log` 的人類輸出、錯誤訊息措辭 | 只要語意相容 |
| C | hash、lock 檔名、內部 instruction 編號 | **不需要一致**，只要不跨版本邊界 |

B 這一層文件其實已經做對了 —— `OUTPUT.md` 明講「`message` 給人看，可以改好懂的措辭；
程式只比較穩定、簡短的 `code`」。把同一個原則推廣到其他人類輸出即可。

C 這一層才是關鍵：**JCS 只有在 hash 會跨版本邊界時才需要。** 現在三個 hash 用途分別是 ——

- **model lock key**：會跨（Python bot 和 C++ bot 要對同一個 lock 檔名）→ 但整個 model lock 建議刪掉（3.1）。
- **tool spec hash**：instruction 本來就已經存了**解析後的完整 argv 與 executable 絕對路徑**，hash 是多餘的。
- **instruction input hash**：recovery 要回答的是「這個 result 屬於哪條 instruction」，遞增的 `seq` 就夠了。

三個都能消掉 → RFC 8785 那一整節可以不做。`JSON.md` 需要留的只有「讀取規則」，
加上 `TOOL_SPEC.md` 錯誤排序要的那一條 **key 依 UTF-16 code unit 排序** ——
那是一句話，不是整套 canonicalization。

### 3.2.2 「一定會做」真正該買的準備

比散文決定性有用得多，而且現在做很便宜、以後做很貴：

1. **每個落盤檔都要有 `format` 欄位。** 三份 raw manifest 和 event 已經有了（`agentos.llm.raw.v1`、
   `agentos.event.v1`），`state.json` 目前沒有 —— 補上。這就是 port 需要的九成：
   C++ 能不能打開 Python 建的 Bot，靠的是這個，不是 hash。
2. **想清楚 Python 與 C++ 共存期。** 一定會有一段時間兩個版本在同一台機器上。至少要定：
   flock 的檔名與語意兩邊必須相同；未知 `format` 一律 fail closed 不猜。
3. **把 policy 收斂成一個接縫 —— 這是 Janet 的準備工作。**
   `RUNTIME.md` 已經定了「Janet 只能對 immutable snapshot 回傳 typed decision」，邊界是對的。
   但現在的 v1 幾乎**沒有 policy**：limit 檢查散在 worker 裡、approval 藏在 step mode 裡、
   全都是寫死的規則。等 C++ 寫完才要找出「哪些是政策」會非常痛。
   建議現在就在 Python 裡留一個 `decide(snapshot) -> action` 的窄函數，即使它一開始只回傳常數。
   Janet port 就變成「換掉一個函數」，而不是「把散落各處的決策找出來」。
4. **純函式與 I/O 分離。** JSON 驗證／composition、argv 組裝、狀態轉移都寫成不碰 process 的純函式 ——
   那些正是 C++ 要重寫的部分，也正是 fixtures 能覆蓋的部分。
   `tooljson/args.py`（「純函式，不碰 process」）已經是這個切法的範本。

### 3.3 `$ref` 的兩個花俏功能

`$ref` 對 tools 和 llm config 是值得的（共用 endpoint 設定、共用工具集是真需求）。但這兩個可以刪：

- **`{"$ref":["a.json","b.json"]}` 的 array 合併**：deep merge 已經由 `local` override 做了一次，
  這是第二套合併機制。兩套合併規則 = 使用者要記兩次優先權。
- **`file.json#part/name` fragment**：它存在的唯一理由是「一個 ref 只能產生一項」，
  所以一個裝了三個 tools 的檔案要寫 `bundle.json#tools/2`。**要使用者數陣列 index 是很糟的 UX。**
  改成按名字選更好：

```sh
./tools add ../shared/bundle.json            # 全部加入
./tools add ../shared/bundle.json read_file  # 只加這一個
```

順帶：`$env` 找不到變數又沒有 default 時，現在是回 JSON `null`「交給 schema 判斷」。
這會讓錯誤在離現場很遠的地方爆出來。改成當場報錯並印出變數名字。

### 3.4 公開狀態 `ready`

文件自己說「`ready` 通常很短暫」。使用者幾乎看不到它，卻必須學它，而且它在 `COMMANDS.md` 的表格裡
佔一整欄，行為是「`run next` → 拒絕：先 pause」。
使用者的困惑會是：我剛剛沒在跑啊，為什麼不能 step？

建議 v1 對外把 `ready` 併入 `working`。內部 journal 照舊區分。這樣公開狀態剩 7 個，
指令表少一欄，也少一類難解釋的拒絕。

唯一的代價：crash 或 worker spawn 失敗後會顯示 `working` 但其實沒人在做事。這個 `status` 補一行
提示就好（例如 `working (no worker; run continue)`）。

---

<!-- archive-original:end -->

<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-02-COMPLEXITY-AND-FIXTURES-A.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-04-VISION-AND-ONBOARDING.md)
<!-- archive-nav:end -->
