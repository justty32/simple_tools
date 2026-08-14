<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-01-OVERVIEW-AND-TOOLJSON.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-03-COMPLEXITY-AND-FIXTURES-B.md)
<!-- archive-nav:end -->

<!-- archive-original:start -->
## 三、可以刪掉的複雜度（依價值排序）

### 3.1 model unload、model resource lock、direct Ollama adapter ← 最大的一筆

目前為了「`llm set model` 要先安全卸載舊模型」，設計付出了：

- 第二把 lock（model resource lock）與固定的 `worker → model → state` 三層 lock ordering
- `RUNTIME.md` 裡最長也最難的一整段 model mutation 流程（拿 lock、放 lock、重驗、再拿、再驗、才 commit）
- `$XDG_RUNTIME_DIR/agentos/models/` 的 lock 目錄、UID/mode 檢查、SHA-256 lock filename
- `model_busy`、`--no-wait`、`unload: not_supported` 這些額外的錯誤路徑
- 一個只為了查 `/api/ps` residency 而存在的 direct Ollama adapter

換回來的是什麼？而且它**本來就不可能正確** —— 文件自己也承認「無法約束其他程式直接呼叫 Ollama」。
Ollama 自己有 `keep_alive`，換模型時本來就會自己換。

更關鍵的是：**freepy 早就解決過這件事了**。`llmkit/proxy` 用 LiteLLM 把 DeepSeek 雲端、遠端 Ollama、
本機 LM Studio 統一成一個 OpenAI 相容端點，「換模型就是換一個字串」。

建議：

- v1 **只保留 OpenAI-compatible 一種 engine**（加上離線測試用的 fake）。Ollama 自己就有 `/v1`。
- 刪掉 `llm unload`、model lock、`model_busy`、lock ordering 那一整段。
- `llm set model` 退回成純粹的 config 修改：只需要 state lock，只需要「沒有 in-flight instruction、
  沒有 pending tool call」這個檢查（這部分要留，它是為了 messages 一致性，不是為了 VRAM）。
- 真的需要控制 VRAM 時，之後再加一個明擺著是 best-effort 的 `llm unload`（背後就是 `ollama stop`），
  而且**不要綁在 `set model` 上**。設定指令產生系統層副作用，違反這份設計自己在別處堅持的
  「`set` 只寫 local override，絕不動共用資源」。

失去的是「切模型時保證舊模型已釋放」。以單機個人使用來說，這個保證值不了它的價錢。

### 3.2 跨語言決定性：不是刪掉，是換一種付法

> **修訂（C++／Janet 重寫已確認一定會做）：** 我原本建議把 JCS 和「三語言一致」延後。
> 前提改變後這個建議收回一半 —— 目標要留，但**付款方式**要換。

問題不在「要不要跨語言一致」，在於現在的付法是：**在程式存在之前，用散文把 byte-exact 行為寫死。**

這有三個結構性缺陷，而且第二節的表格已經證明它們不是假想：

1. **散文規格是行為的第二份手抄本**，而手抄本一定漂移。tooljson 和 agent-machine 兩份都是人手寫的
   跨語言權威，都寫得很仔細，結果在 `1.0` 該變 `1` 還是 `1.0` 上就已經分岔了。
2. **散文無法被機器檢查。** 沒有任何 CI 能驗證「文件說的」和「Python 做的」一致。
   等 C++ 寫出來對不上時，你無法判斷是誰錯。
3. **還沒跑過的規則有相當比例是錯的。** 現在把它凍進散文，C++ 會忠實地繼承 Python 從來沒機會暴露的錯誤。

正確的付法是 **fixture corpus**，而且理由正是「Python 是 oracle」這句話本身：
如果 Python 是 oracle，那 oracle 的**輸出**就是規格，不需要人再手抄一份。

```text
散文  →  說明意圖、講清楚為什麼這樣定、列出反直覺的 case
fixtures →  凍結行為本身，(input → expected output) 全部 checked in
文件裡的契約句 →  「C++ 必須重現 fixtures/ 的每一筆」
```

fixtures 不可能跟實作漂移（用實作產生、進版控、CI 驗）、在新語言可機械驗證（跑一遍 diff）、
而且 Python 一旦能跑就幾乎免費（`python -m agent_machine._checks --dump-fixtures`）。

**這個模式 repo 裡已經有了**：tooljson 的「45 關 exec roundtrip」加上 `examples.build("/tmp/demo")`
把假工具連 spec 一起寫出來 —— 那就是 fixture generator。照抄它。

順序是：**Python 實作 → 用起來、讓格式在真實使用中穩定 → 凍成 fixtures → 才動手 port。**
不是「先凍散文 → 實作 → port」。凍結點在中間，不在最前面。

<!-- archive-original:end -->

<!-- archive-nav:start -->
[上一頁](OPUS-ADVICE-01-OVERVIEW-AND-TOOLJSON.md) · [索引](OPUS_ADVICE.md) · [下一頁](OPUS-ADVICE-03-COMPLEXITY-AND-FIXTURES-B.md)
<!-- archive-nav:end -->
