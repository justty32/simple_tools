# aos-diy

**從零手寫一次 aos。C++26。使用者寫程式碼，AI 當導師。**

隔壁 [`../aos-simple/`](../aos-simple/) 已經有一份能跑的完整實作
（約 2000 行，390 項測試全過）。這裡不是要寫第二份，是要**自己走一遍**——
所有那些「踩過才知道」的東西，讀跟做是兩回事。

---

## 給接手的 AI：先讀這三份

按順序：

1. **[docs/TUTOR.md](docs/TUTOR.md)** — ★ 你的操作規則。**先讀完再開始。**
   最重要的一條：**使用者寫全部的程式碼，你不寫。**
2. **[docs/CURRICULUM.md](docs/CURRICULUM.md)** — 十二個 Stage，
   每個有目標、驗收條件、該給的陷阱
3. **[docs/TRAPS.md](docs/TRAPS.md)** — 陷阱總表。你要全部記得，
   但按 Stage 給，不要一次倒出來

然後看 **[PROGRESS.md](PROGRESS.md)** 使用者走到哪，從那裡繼續。

---

## 這是在做什麼

一個本機的命令執行機器。核心是這五行：

```cpp
for (;;) {
    auto call = queue.take();   // 取一個，空的就擋著等
    if (!call) break;           // 取到哨兵 = 收工
    handle(*call);              // 交給執行者
}
```

一次呼叫是一個固定形狀的 struct：

```cpp
struct Call {
    std::vector<std::string> argv;
    std::string stdin_path, stdout_path, stderr_path;
    std::string exit_path;
    std::string cwd;
    std::optional<std::string> env;    // 可選
    std::optional<std::string> user;   // 可選
};
```

★ 三條串流不是位元組，是**位址**——由執行者去讀去寫。
★ 結論落在 `exit` 檔：**檔案不在 = 還不知道；在 = 這一次的結論。**

其他每一樣（分流、併發、優先權、逾時、agent、輸入層）都是插在
「queue ／ loop ／ Executor 介面」這三個縫上的東西。

要看完整的演進與理由：[`../aos-simple/docs/OVERVIEW.md`](../aos-simple/docs/OVERVIEW.md)。

---

## 為什麼值得手寫一遍

因為這套東西的價值幾乎全在**那些不明顯的決定**上，而那些決定看程式碼是看不出來的：

- `exit 137` 和 `SIGKILL` 為什麼分不開，怎麼分開
- 為什麼 fork 前一定要 `fflush`（而且一般建置根本看不到症狀）
- 為什麼 `fsync` 要兩次、順序不能反
- 為什麼砍 pid 不夠、要砍行程群組
- 為什麼「收工」不等於「把待辦丟掉」
- 為什麼 `Unknown` 不能當失敗

這些每一條都是踩出來的。自己踩一次，就變成自己的。

---

## 語言

**C++26**，GCC 16.1.1（這台已確認 `-std=c++26` 可用）。

有：`std::expected`、`std::jthread` ＋ `stop_token`、`std::print`／`format`、
`std::span`、`std::optional`（含 monadic ops）。

**沒有**：`std::execution`（senders/receivers），GCC 還沒實作——不要往那邊設計。

> ⚠ 新東西不要為用而用。`std::expected` 在 `validate()` 上是真的比較好；
> 但 `fork`／`exec`／`waitpid` 那一層是純 POSIX，硬套現代 C++ 只會更難讀。

---

## 目錄

```
aos-diy/
  README.md        這份
  PROGRESS.md      ★ 走到哪。每個 Stage 結束都要更新
  docs/
    TUTOR.md       導師的操作規則
    CURRICULUM.md  十二個 Stage
    TRAPS.md       陷阱總表
  src/             你的程式碼（現在是空的）
  test/            你的測試（現在是空的）
```

---

## 規矩

**看參考實作的時機**：自己那個 Stage 的測試通過**之後**才去看
`../aos-simple/` 對應的檔。那時候看是「原來還可以那樣處理」；
之前看就只是抄。
