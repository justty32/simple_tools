# 評估方法與 C／C++ 分工

## 不只問「能不能寫」

C 和 C++ 當然都能完成這套系統；有用的問題是：

1. **契約穩定嗎？** 行為能否用 JSON、bytes、argv、event trace 表達並重播。
2. **native ownership 有收益嗎？** 啟動、記憶體、部署、OS 整合、可嵌入性或可驗證性是否真的改善。
3. **動態性要留在哪？** Python callable、type hint、docstring、importlib、provider SDK 是否仍在主路徑。
4. **TCB 會變好嗎？** C++ 是否縮小權限與狀態空間，還是只把 Python bug 換成 UB／race／ABI bug。
5. **平台證據夠嗎？** Windows、Linux、process、path、signal、pipe、SSE 是否實跑，而非只有 API 存在。

## 評級

| 等級 | 含義 | 遷移策略 |
|---|---|---|
| A | 穩定資料／純規則，native ownership 有明確收益 | 優先 C++ 固化，保留 differential oracle |
| B | 核心適合，side effect 或平台語意需 adapter | C++ reducer/value core + typed adapter |
| C | 技術能做，但變動、生態或總成本不划算 | 先留 Python／proxy，wire 邊界先穩定 |
| D | Python 特有 reflection、純 prototype 或薄 launcher | 不追求語意等價移植 |

評級不是「C++ 能不能寫」，而是「現在該不該讓 C++ 成為 production owner」。

## C 與 C++ 不視為同一選項

### C++ 適合擁有

- `AgentPath`、policy、budget、Round／Step、tool spec 等強型別 value object。
- `std::variant`／tagged state、RAII、`std::expected` 式錯誤、filesystem 與並發 primitive。
- JSON contract、event reducer、native HTTP/process/storage/FUSE adapters。
- 需要同時提供 executable 與 library 的部署形狀。

### C 適合擁有

- 跨 compiler／語言的穩定 ABI。
- 很薄的 syscall／library shim。
- 明確 ownership 的 bytes buffer、handle、callback table。

### 純 C 不適合成為主核心的原因

本系統的資料大量是 optional field、開放 registry、nested JSON、typed error 與 state transition。純 C 可以做，但每個 value 都要自行處理 tag、allocation、cleanup 與 error path；這不會帶來 end-to-end LLM latency 改善，卻會擴大 review 面積。除非目標是極小 embedded footprint 或既有 C host，否則 C++ 是較合理的實作語言，C 是邊界語言。

## 固化門檻

某個 slice 進 native 前至少要有：

- versioned schema 與 unknown-field policy。
- 正常、邊界、錯誤、crash/retry、重播 fixtures。
- pure decision 與 effect 分離。
- Python 與 C++ 對相同 fixture 的 normalized output／trace 等價。
- ownership、threading、deadline、cancel、idempotency 的明文規則。
- clean build、Linux CI、Windows 支援表與 reproducible dependency lock。
- sanitizer／fuzz 能進入 parser、path、protocol 與 lifecycle state machine。

如果 message shape 還每週改、測試只能連真模型、或行為必須靠 Python object identity 才存在，就先不要搬。

## 證據強度

由強到弱：

1. differential／wire／sanitizer／fuzz 測試。
2. 本機乾淨 native build 與現有 Python smoke。
3. repo 中已存在的 C++ production-style 程式（目前是 `dcap`）。
4. library／標準 API 存在。
5. 「C++ 比 Python 快」等沒有 workload benchmark 的泛稱。

本報告不把第 4、5 層當作已完成證據。

## 已實作與已規劃分開

已實作：`llmkit/tooljson`、`llmkit/llms`（含簡單 preset loader）、`agentloop`、
`base_tools`、`exec_tools/discover.py`、`shells`、prototypes。

仍是設計：agent identity、communication、runtime、team、memory、introspection、agentfs、Round control 擴充。後者只能評架構 fit，不能用文件頁數假裝 migration progress。

## 主要來源

- [freepy ROADMAP](../../../../freepy/ROADMAP.md)
- [tooljson FORMAT](../../../../freepy/llmkit/tooljson/FORMAT.md)
- [agentloop README](../../../../freepy/agentloop/README.md)
- [Janet 評估方法](../janet/01-method.md)
- [dcap C++ 專案](../../../../dcap/README.md)
