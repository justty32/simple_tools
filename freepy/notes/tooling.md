# base_tools、tooljson 與 llmkit 決策筆記

本頁保存 2026-08-08 至 2026-08-09 的工具與分層決策；目前介面以相鄰 README 為準。

## base_tools 接進 tooljson 時抓到的安靜錯誤

`base_tools/tools.json` 由 `specs.py` 產生，四個工具成為 `_type: "python"` 的 spec。能力從
import 清單變成設定檔後，才可與 exec 型外部工具混用。

實作時發現 `python -m base_tools.specs` 的 `__module__` 是 `"__main__"`，`from_tool()` 從檔名
猜成 `"specs"`，但正確名稱是 `"base_tools.specs"`。JSON 可以正常寫出，直到另一個 process
讀取才會 import 失敗，因此特別危險。

`tool.py` 的 docstring 已規定「反推不出來就丟，不要猜」。修正是由檔案路徑一路收集帶
`__init__.py` 的父目錄（`_dotted_name()`）；同時阻止 `_source_file()` 的空字串被
`os.path.abspath("")` 當成 cwd，產生假 module name。

這再次證明反直覺的理由應留在程式 docstring：它能在後續實作違反契約時直接暴露矛盾。

## tooljson 加入第二種 `_type`

加入 `_type: "python"` 時，`spec.py`、`registry.py`、`bind()` 和 `_version` 都不用改。外殼只讀
所有類型共有的保留鍵，其餘交給 `_type` parser，因而通過第二種 execution type 的實證。

唯一需要整理的是 `invoke.py`：其 docstring 明說該檔屬於 exec，但 Python 型別也需要
`MAX_OUTPUT`、`clip`、`decode`。這些共用文字處理因此移到 `text.py`，避免跨界 import 或複製常數。

## 四層疊上去，順序不能顛倒

1. Proxy 用 `litellm.yaml` alias 統一雲端、遠端 Ollama 與本機 LM Studio。
2. `llms` 加 preset 形成一步一個 `Reply` 的 bot；此層只會說話。
3. `tooljson` 用 JSON 描述能力；bot 會做事，但仍由人逐步推進。
4. `agentloop` 才負責自主推多步與決定停止。

下層錯誤會在上層表現成無法辨認的症狀，例如模型 caps 宣告錯誤可能看似 agent 不肯叫工具。
因此 caps 必須實測，preset 只是第 2 層的執行輸入，不是獨立研究系統。

## llmkit 為何是三塊

`llms`、`proxy`、`tooljson` 從 freepy 抽成地基，其餘 `base_tools`、`exec_tools` 掃描與 prototypes
建在上面。分界依據是介面是否穩定，而不是功能是否寫完：路徑規則仍會變的 `base_tools`
留在上層；一旦有第二語言實作便難以更動的 tooljson 格式先定型。

`proxy` 沒有 Python 程式仍獨立成一塊，因為 YAML 中的能力設定是實測結論，不只是 configuration。

`tooljson` 的 `_type` 改為 registry 也在這時完成。若把 `TYPES = ("exec",)` 寫死，第三方新增
execution type 就必須改 llmkit。執行路徑也由 `invoke.run()` 改為 `Spec.run()` → `body.run()`，
讓註冊的新類型不只讀得進來，也真正執行得了。
