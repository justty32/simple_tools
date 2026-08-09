# modelcards

把每顆模型的**官方建議參數**和**能力**記成一份 JSON，配一個小 lib，
讓用 `llms` 的時候一行就套上去：

```python
from llms import LLM
from modelcards import engine_for

bot = LLM(engine_for("lm-qwen3.5-9b"), system="你是個惜字如金的助手")
```

欄位規範在 [FORMAT.md](FORMAT.md)，資料在 [`cards/`](cards/)（一顆一份）。
資料本身是**另一個 agent 上網查來的**，任務書留在 [RESEARCH.md](RESEARCH.md)，
之後要補模型照著再發一次車。

## 先立一條：查來的 ≠ 打過的

`proxy/README.md` 已經抓到 litellm 兩次謊報，一次謊報成 `False`、一次謊報成 `True`，
所以這個 repo 的規矩是**要宣告一件事之前先實打一次**。網搜來的東西是第三個來路不明
的資料庫，直接寫進 `litellm.yaml` 的 `model_info` 等於同一個坑再踩一次。

所以 card 裡分兩格，而且**分得夠死**：

| | 誰填的 | 拿來做什麼 | 形狀 |
|---|---|---|---|
| `claimed` | 搜尋 agent，照官方文件 | **情報**。告訴我們該去打什麼、預期看到什麼 | `{"v", "src"}` |
| `verified` | 我們自己實打過 | **結論**。只有這格能變成 `Engine(caps=...)` 的 override | `{"v", "on", "how"}` |

`caps_for()` 預設**只吐 `verified`**，要吃 `claimed` 得明講 `trust="claimed"`。
**這條規矩是寫在程式裡的**（`card.py` 的 `caps()`），不是只寫在文件裡 ——
連形狀都不一樣，貼錯格子會當場被 `check.py` 擋下來。

參數那邊沒有這個問題（`temperature` 送錯不會被擋，只會變笨），所以官方建議值直接用；
但每個值一樣要有 `src`，因為出錯時第一件事是回去看那個值哪來的。

## 介面

```python
find(alias_or_id)                 -> Card        # alias 或 id 都認，找不到就丟
params_for(alias, **override)     -> llms.Params # 建議參數，override 蓋掉
caps_for(alias, trust="verified") -> dict        # 七欄，給 Engine(caps=...)
engine_for(alias, trust=..., **kw) -> llms.Engine # 上面兩個組好，一步到位
```

`cards()` 列出全部、`reload()` 改完 JSON 不用重開 python。

**`params_for` 有一件事非做不可**：`top_k` / `min_p` / `repetition_penalty` 這些不在
`llms.Params` 的欄位裡，會被自動包進 `Params.extra` 的 **`extra_body`**。原本以為這類
參數的風險是被 litellm 的 `drop_params: true` 無聲吞掉，結果 2026-08-09 對 LM Studio
實打發現死得**更早更大聲**：攤成頂層 kwarg 的話，openai SDK 在送出前就
`Completions.create() got an unexpected keyword argument 'top_k'`。包進 `extra_body`
才會原封不動進 JSON body。

`drop_params` 那個坑仍然在，只是排在後面。`Card.needs_allowlist(mode)` 列出「這顆模型
有哪些參數得靠 `allowed_openai_params` 才送得到」，補進 yaml 時照著抄。

## 驗收是可執行的

```sh
cd freepy && uv run python -m modelcards          # 全離線煙霧測試，不花錢
cd freepy && uv run python -m modelcards table    # 人看的表
```

**表不手寫第二份。**手寫的表一定會跟 JSON 不同步，而不同步的文件比沒有文件更糟，
所以 `table` 子命令從 JSON 生出來，README 裡不放第二份拷貝。

煙霧測試驗的東西全部離線：每份 card 形狀合法、每個值有 `src`、`sources` 沒有斷號、
alias 不撞名、`params_for` 吐得出 `Params`、`caps_for` 預設不吐 `claimed`，
以及 **alias 集合跟 `proxy/litellm.yaml` 對帳**。對帳那關兩邊不對稱是刻意的：
卡指到 yaml 裡沒有的 alias 算**錯**（打錯字），yaml 有而卡還沒填的只印進度不算失敗 ——
**一直紅的關卡會被訓練成無視**。

## 現在填到哪

九張卡、13 個 alias 都有卡，形狀全綠。實打（`claimed` → `verified`）還沒做完：

- **deepseek 雲端、LM Studio 三顆**：打過了，`verified` 有東西。
- **ollama 那四顆**（`deepseek-r1-8b`、`gemma3-1b`、`qwen2.5-14b-instruct`、
  `qwen3-32b`）：`verified` 還是空的。它們跑在 192.168.1.146，要那台機器連得到。

`verified` 空著**不是 bug** —— 空著代表「還沒打」，`caps_for()` 就吐不出東西，
讓 `Engine` 回去問 proxy，這正是預期行為。填的方法一顆一顆照 `claimed` 去打，
對得上就寫進 `verified`，對不上的就是第三個謊報，記進 `proxy/README.md`。

## 放哪：先在 `freepy/`，不是 `llmkit/`

界線是「**介面還會不會變**」。card 的欄位一定還會動（`modes` 該怎麼切、`verified`
要記多細，現在都是猜的），所以先長在上面這層，跟 `exec_tools/discover.py` 一樣，
穩了再搬進 llmkit。

方向維持單向：**`modelcards` import `llms`，`llms` 不知道 `modelcards` 存在。**
抽掉 modelcards，llms 照樣能用 —— 跟 tooljson 對 llms 的關係一樣。`llms` 是延遲
import 的，所以只讀資料不需要裝 openai；lib 本身不動 `sys.path`，補路徑是
`__main__.py` 的事（package 偷改全域狀態比 import 錯誤更難查）。

## 一個還沒決定的：`-nothink` 只在經過 proxy 時成立

2026-08-09 實打抓到的。`engine_for("lm-qwen3.5-9b-nothink")` 直接打 LM Studio
**照樣會思考** —— 關掉思考靠的是 `reasoning_effort: "none"`，那個焊在
`litellm.yaml` 的 `litellm_params` 裡，不在卡上。手動補上就 think 長度 0。

兩條路：把 `reasoning_effort` 也記進 `modes`（卡自我完備，不經 proxy 也對），
或維持現狀（那是 runner 的旋鈕，不是模型作者的建議）。**先維持現狀**，
等第二顆思考模型實打完再看它是不是同一個形狀 —— 一個例子不夠定規則。

## 先不做

- **不自動改 `litellm.yaml`**。生 `model_info` 片段可以，貼進去是人的決定。
- **不做線上更新／自動重查**。card 是手動資料，過期了就照 RESEARCH.md 再跑一次。
- **不收 pricing / rate limit**。那是帳務，不是「這顆模型怎麼用」。
- **不碰 `llms` 的任何一行**。這包純粹長在它外面。

## 檔案分工

| 檔案 | 負責 |
|---|---|
| `card.py` | Card 資料結構、讀一份 JSON、剝出處拿值 |
| `check.py` | 形狀驗證，格式規範可執行的那一半 |
| `store.py` | 讀整個 `cards/`、alias 索引、撞名偵測 |
| `__init__.py` | 上面那四個函式 |
| `__main__.py` | 入口：煙霧測試 ＋ `table` 子命令 |
| `smoke.py` | 離線煙霧測試的關卡 |
| `table.py` | 從 JSON 生 markdown 表 |
| `FORMAT.md` | card 的欄位規範 |
| `RESEARCH.md` | 給搜尋 agent 的任務書 |
| `cards/*.json` | 資料，一顆一份 |
