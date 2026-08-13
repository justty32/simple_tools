# Agent Machine 實作規劃

**狀態：2026-08-13 起成為主線開發。** 先用 Python 做可執行、可故障注入的參考實作；
資料格式與狀態轉移穩定後，再以 C++23 實作主程式，內嵌 Janet 表達可替換的政策。

若要先看圖與施工順序，從 [HTML 實作導覽](../../guides/agent-machine/index.html) 開始。

目標不是重做 Linux 或 Windows，而是在現有 OS 上增加一層「agent 用的機器」：函數、執行狀態、
模型、工具與外部資源都能用同一個路徑空間觀察；所有正式修改都能追蹤、恢復與版本化。

## 已決定的做法

- 對使用者呈現普通的檔案、目錄與 shell；內部把 write 解析成有版本、有型別的操作，再驗證和提交。
- 函數保持單一檔案 `bot-a`，增加的狀態放在同層 `.bot-a/`。不建立 `/proc`，也不預設 Unix 目錄布局。
- 路徑是名稱，不是永久身分。每個節點另有不隨 rename 改變的 `node_id`；已開啟的 handle 還綁定
  generation 與權限，避免舊操作誤寫新物件。
- 一次 LLM 呼叫是一個 Step，也是一條 LLM instruction；工具執行是另一類 instruction。
  一個 Round 是函數的一次完整呼叫，可以包含多個 Step 與工具執行。
- LLM 回覆只是提案。只有核心驗過格式、權限、預算、generation 與工具效果後，狀態才會改變。
- VFS generation 是目前狀態的正式版本；journal 負責斷電恢復與稽核；Git 負責有意義的 checkpoint、
  branch、diff 與 rollback。三者用途不同，不互相冒充。
- 模型切換只在沒有 in-flight instruction、pending tool call 或 writer lease 的安全邊界進行。使用
  Ollama 時，scheduler 還要確認沒有其他 Function 使用舊模型，才能 unload、確認釋放並載入新模型。
- dormant function 不佔一條 OS thread。中央 scheduler 保存 queue 與 lease，worker 只在有工作時喚醒。
- Python 是規格探索與測試 oracle；C++ 擁有不可破壞的規則、儲存、排程與 OS adapter；Janet 只決定
  可替換政策，不能繞過 C++ 直接做 I/O 或提交狀態。

## 系統輪廓

```text
shell / Python API
        |
        v
namespace + typed operations
        |
        v
machine core ---- journal / VFS generations / Git checkpoints
        |
        +---- scheduler ---- LLM executor
        |                 `-- tool / file / process executor
        |
        `---- policy (Python prototype; final Janet)

host OS owns real files, processes, network, devices and isolation
```

`machine core` 是唯一可以發布新 generation 的元件。Executor 可以很慢、失敗或在另一個 process；
它只回傳結果與效果證據，不直接改核心狀態。

## 幾個縮寫

| 寫法 | 本計畫中的白話意思 |
|---|---|
| VFS | 以 path 操作不同資料／服務的統一介面，不一定是真實磁碟 |
| root generation | 整個 namespace 一次完整快照的版本號 |
| journal | commit 前後的復原紀錄；不是另一份 current state |
| CAS | 只有版本仍等於預期值才更新，否則回 conflict |
| lease | 有期限的執行權；過期 worker 的結果不能直接提交 |
| reconcile | 外部動作結果不明時，先查實際狀態再決定 |
| residency | 某模型目前是否仍載入本機記憶體／VRAM |
| GC | 找出已無 live reference 的物件，先封存再回收 |

## 文件順序

1. [01-model.md](01-model.md)：名詞、資料結構、狀態機與不可破壞的規則。
2. [02-filesystem.md](02-filesystem.md)：VFS、companion directory、交易、Git、refs 與 GC。
3. [03-execution.md](03-execution.md)：executor、排程、context、模型切換與失敗恢復。
4. [04-python.md](04-python.md)：Python package、CLI、現有 FreePy 重用方式與原型切片。
5. [05-cpp-janet.md](05-cpp-janet.md)：最終 C++/Janet 邊界、ownership、執行緒與 build。
6. [06-delivery.md](06-delivery.md)：PR 順序、測試矩陣、每階段 gate 與完成定義。
7. [07-evolution.md](07-evolution.md)：自我改進閉環，以及 EvoMap/GEP 值得借與不可直接採用的部分。
8. [SOURCES.md](SOURCES.md)：本 repo 與四個外部 repo 的盤點範圍、採用依據與衝突處理。

## 第一個里程碑（PR 1–3）

第一個成果不是「會聊天的 OS」，而是一個由三個小 PR 組成的完全離線閉環：

```text
建立 bot-a + .bot-a/
  -> 提交一個 deterministic read/write instruction
  -> 在 staging tree 產生修改
  -> 驗證後發布新 VFS generation
  -> 非同步發布 Git checkpoint
  -> 從 journal 重播得到完全相同的狀態
```

PR 1 只驗 pure model；PR 2 才用 directory-backed store 在每個寫入點模擬 crash；PR 3 加 Git。
這一層過關後才接真 LLM，否則模型延遲與網路錯誤會掩蓋
最基本的資料一致性問題。

## 這一輪不做

- 不先做 kernel module、FUSE 或 9P mount；provider API 穩定後再加 transport。
- 不宣稱 Git 能 rollback 已寄出的信、HTTP POST 或其他外部副作用。
- 不把現有 `Handle`、Python callable、thread、socket 或 Janet VM heap 直接持久化。
- 不先做分散式共識；第一版是單機、多 worker、可跨重啟。
- 不以模型自述的信心或「完成了」作為正確性證據。

## 舊文件的地位

[`../future/agent-machine/`](../future/agent-machine/README.md) 保留較完整的研究模型與資源分類；
它不再決定實作順序。本目錄是新的主計畫。實際已存在的 API 仍以程式、測試與 package README
為準；計畫不能把尚未完成的功能描述成現況。
