<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-06-LINUX-FILESYSTEM-AND-STORAGE.md) · [索引](OPUS_ADVICE.md) · 下一頁：終點
<!-- archive-nav:end -->

<!-- archive-original:start -->
## 十一、tools 的目標形態：檔案存取 + argv 統一介面（已確認方向）

這個方向是對的，而且它比聽起來便宜 —— 幾乎不需要新機制。但它會改變三件事的優先順序，
其中兩件是**修正我自己前面的說法**。

### 11.1 它證明了 argv mapping 是該投資的那一半

把 `_extra` 的欄位按這個目標分類，會分得很乾淨：

| | 欄位 | 地位 |
|---|---|---|
| **永久** | `exec`、`argv`（position/flag/separate/repeat）、`ok_exit`、`timeout`、`cwd` | 這就是「argv + 一個 linux 檔案」本身 |
| **過渡** | `stdin`、`stdout.clip`、`stdout.max_bytes`、`stderr.mode`、`limits` | 全都是「stdout 當結果」的產物 |

所以第二節「直接依賴 tooljson」不只是省工 —— tooljson 已經測過 45 關的那一堆
（argv 排序 tiebreak、boolean flag、repeat、型別轉字串），**正好就是永久的那一半**。
自己重寫等於把已驗證的永久部分換成未驗證的，同時繼承過渡部分。

順帶一個一致性的觀察：這個方向也讓「v1 只收 `exec` 和 `builtin`」從一個 v1 限制，
變成一個**有原則的最終決定** —— 因為如果一切都是 argv + 檔案，`exec` 本來就是唯一需要的 `_type`。
而 `builtin` 是唯一該有的例外，理由也很乾淨：`ask_user` 讀的不是檔案，是人。
（Plan 9 的講法：它是 user 這個 device 的介面。這個類比之後可能有用，但 v1 不必動它。）

### 11.2 唯一需要現在想清楚的：模型看到什麼

tool 寫了 10MB 到檔案，模型看到什麼？這是這個方向真正的設計問題，其餘都是既有機制。

```text
現在：  result = stdout，截到 65536 bytes
目標：  result = 一行摘要 + 路徑；要看內容再叫 read_file(path, offset, limit)
```

價值不只是美學，有三個實質好處：大結果不炸 context；結果持久、可以分段重讀；
而最關鍵的是 —— **tool A 的輸出可以直接餵給 tool B，完全不經過模型的 context**。
這是「檔案空間即記憶體」在實務上真正兌現的地方。

而它需要的東西少得驚人，三件都不用改格式：

1. 一個明確的 workspace 概念（見下）
2. catalog 裡有 `read_file(path, offset, limit)`（第 5.3 節本來就要做）
3. 一個約定：tool 把資料寫檔案、stdout 只印一行摘要

第 3 點是約定不是機制，所以可以現在就在自己寫的 tool 上開始遵守，不必等任何實作。

### 11.3 修正第六節：workspace 不是小旋鈕，是中心概念

第六節我把 cwd/workspace 當成「一個旋鈕、預設 Bot root、寫一行就講完」。
在這個方向下**那是錯的** —— workspace 就是 tools 之間傳遞資料的媒介，是這台機器的記憶體。

所以它要提早定，而且要明確：

- workspace 是 **Bot 的明確設定**，不是埋在 tool spec 裡的 `cwd: null` 語意。
  `_extra.cwd: null` 應該解讀成「用 Bot 的 workspace」，而不是「Bot root」。
- 每個 Run 有自己的 scratch（例如 `.agentos/run/scratch/`），隨 run 一起 archive ——
  中間產物的 provenance 就順便有了，不必另外設計。

### 11.4 修正第 10.3 節：GC 的類比重新有了指涉對象

我在 10.3 說「沒有 object graph，GC 的最終形態就是一行保留政策」。
在檔案介面下這句話要收回一半：中間檔案會**真的**累積，Lisp machine 那個 GC 類比重新有了對象。

但 KISS 的答案仍然不是 reachability 分析。是：

```text
每個 run 一個 scratch 目錄  +  archive 只留最近 N 次
```

差別在於，現在這不只是清垃圾，它同時定義了「一次工作的產物範圍」。
真的需要跨 run 保留的檔案，使用者自己寫到 workspace，不進 scratch —— 這條界線比 refs／pin 好懂太多。

### 11.5 一件要先講清楚的事：workspace 看起來像邊界，但不是

`EXEC.md` 很誠實地寫了 exec 擁有完整使用者權限、沒有 sandbox。但當 tools 全部改成透過檔案溝通、
而且有一個叫「workspace」的目錄時，**它會看起來像一個邊界**，使用者會很自然地以為工具只碰得到那裡。

兩條路：在那之前接上 `docs/research/sandboxing/` 的結論，或者在 workspace 這個詞第一次出現的地方
就把「這不是邊界」寫在旁邊。在真的有隔離之前，第七節那個 `confirm: true` 仍然是 CP 值最高的防線。

---

## 十二、還沒定案的事

1. **名字**：專案叫 Agent Machine，介面文件叫 AgentOS，binary 叫 `agentos`。挑一個。
   （順帶：對一個不管理硬體的東西叫 OS，會一直被問「kernel 在哪」。捨棄 VFS 之後更是。）
2. **tools 是依賴 `tooljson`，還是正式分叉？** 這是第二節在問的那個決定，也是現在唯一有時效性的一題 ——
   兩份規格每多活一天就多漂移一點。若選分叉，至少今天就把 `_version` 改掉。
3. **這是給誰用的？** 全部文件都在講「怎麼做」，沒有一句講「誰、為了什麼」。
   是 (a) 你自己的個人 coding agent、(b) 大量小自動化 bot 的底座、
   還是 (c) Agent Machine 願景的研究載體？
   目前的**文件**在服務 (c)，但**介面**在服務 (a)/(b)。捨棄 VFS 已經把天平推向 (a)/(b)，
   文件還沒跟上。確定這一點，上面一半的取捨會自己有答案。

---

## 附：如果只做三件事

1. 刪掉 model unload / model lock / direct Ollama adapter（第 3.1 節）。
2. tools 直接用 `tooljson`，或至少把版本號分開並列出差異表（第二節）。
3. 讓 `agentos new bot-a && ./bot-a/start "hello"` 開箱就得到真的答案，
   並附一個不需要 LLM 的 `--demo` bot（第 5.1、5.4 節）。
<!-- archive-original:end -->

<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-06-LINUX-FILESYSTEM-AND-STORAGE.md) · [索引](OPUS_ADVICE.md) · 下一頁：終點
<!-- archive-nav:end -->
