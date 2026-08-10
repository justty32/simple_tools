# prototypes

原型區。**這裡的東西是拿來跑一輪、看它撞到什麼，然後被歸納掉的**，不是拿來用的。

規矩跟其他包相反：一個檔案自己完備、可以又臭又長、可以重複造輪子。
等同一件事在這裡出現第二次、第三次，才把它抽成 `base_tools` / `exec_tools` 那種
有 README 有分工的標準庫。先有原型，再有規範 —— 反過來做只會憑空發明一堆用不到的抽象。

跑之前先把 LiteLLM proxy 起來（見 [llms/README.md](../llmkit/llms/README.md)）。

---

## plan_shell.py — 翻譯器

把一句人話翻譯成一串 shell 指令。**只翻譯，不執行。**

```bash
uv run python prototypes/plan_shell.py --root /tmp/ws "把 a.txt 移到 backup/"
echo "把 a.txt 移到 backup/" | uv run python prototypes/plan_shell.py --root /tmp/ws
uv run python prototypes/plan_shell.py --model deepseek-chat --root /tmp/ws "..."
```

這是「純工具」的第一個樣本。重點不在模型聰不聰明，在**它對外長得跟 `grep` 一樣**：

| | |
|---|---|
| stdin / argv | 人話 |
| stdout | 翻譯結果，只有這個 —— 乾淨到可以 `> plan.sh` |
| stderr | 過程（模型探查了什麼），給人看的，不是產物 |
| exit 0 | 翻出計劃了 |
| exit 2 | 資訊不足，stdout 是它要問你的問題 |
| exit 1 | 壞掉了 |

裡面塞了一顆 LLM，但外面看不出來 —— 這正是 note 說的「把 LLM 當函數使用，
只是產出結果不固定，而且執行時間較久」。

### 模型的三個出口

就是 note 2026-08-06 第 24 行那組「通用的」工具：

| 工具 | 意思 | 後果 |
|---|---|---|
| `inspect` | 先看現場（唯讀指令） | 結果餵回去，再想一步 |
| `plan` | 翻得出來了 | 吐 shell，exit 0 |
| `ask_user` | 你講得不夠清楚 | 吐問題，exit 2 |

`plan` 和 `ask_user` **故意沒有實作**。它們不是函式，是這支工具的兩個出口；
存在的意義只是讓模型能結構化地說「我翻完了」跟「我翻不了」，
而不是逼我們去猜它那段散文裡哪幾行才是指令。

`inspect` 則是 note 第 20–21 行講的那件事：模型在交出計劃**之前**得先看現場，
不然它是閉著眼睛規劃。這些探查是中間結果，不進最終產物。

### 為什麼翻譯和執行要分開

翻譯是**不確定的**（模型會錯），執行是**不可逆的**（`rm` 收不回來）。
綁在一起等於把不確定性接到不可逆性上。分開之後，中間那道關卡是人 ——
你看得到那份 shell 再決定要不要跑。

---

## 第一輪：Windows，2026-08-07

`ollama-qwen3-32b`，任務「把 a.txt 移到 backup/」。翻出來的東西是對的，
但過程露了兩個洞：

### 1. 這套東西只服務 POSIX —— 這一輪把它定成契約了

模型送出的探查指令是：

```sh
test -f a.txt && echo '...' ; test -d backup && echo '...'
```

`base_tools.run_shell` 走的是 `subprocess.run(shell=True)`，在 Windows 上等於
`cmd.exe /c`。cmd **不認得 `;` 是指令分隔符**，所以分號後面那半段被當成前面 `echo`
的參數吞掉了。回傳 `exit 0`、沒有任何錯誤。

結果就是：**模型問了兩個問題、只拿到一個答案，而且它不知道**。
它從沒確認過 `backup/` 存不存在，卻照樣規劃了 `mv a.txt backup/`。
這次剛好資料夾存在所以沒事 —— 換一次就是一個假 exit 0 蓋著的錯誤計劃。

安靜失敗比報錯危險得多。

**結論：整套只服務 POSIX，Windows 不支援。** 不做語法翻譯層 —— 翻不完，
而且翻錯一樣是安靜的。`base_tools.run_shell` 現在在非 POSIX 上直接回 Error 不執行，
理由寫在 [base_tools/README.md](../base_tools/README.md)。在 Windows 開發就進 WSL。

會走到「定契約」而不是「修 bug」，是因為這個前提本來就寫在 note 裡了
（「不做規範了，畢竟 linux 該有的都有」），只是沒人講明它是**前提**而不是**觀察**。
原型的價值就在這 —— 讓一個沒說出口的假設用壞掉的方式現身。

### 2. 思考型模型的空回合會白燒掉探查次數

qwen3-32b 前兩步回的是「有 `reasoning_content`、`content` 空、也沒有 tool_calls」。
現在的迴圈把它當成「模型在講廢話」，推它一把再來一次 —— 六步的預算白花兩步。
能自己恢復，所以先擱著；等回合經濟真的變成問題再處理。

### 3. 兩個小的

- 產出的計劃是英文的，system prompt 是中文。小模型的輸出語言跟著任務領域跑，
  shell 相關的訓練資料幾乎都是英文。要中文註解得明講。
- 模型探查過了，就把 plan 裡「先確認來源存在」那步省掉，但**註解留著** ——
  `# Confirm source file exists` 底下直接是 `mv`。這其實是它推理正確
  （已經確認過了）但沒把註解一起改。產出物會自相矛盾。

---

## 第二輪：WSL（真 POSIX），2026-08-07

同一顆模型、同一個任務。先確認第一輪的結論成立，然後撞到一個新的、比較深的問題。

### POSIX 那條修對了

三次 `inspect`，三個問題三個答案，一個都沒掉：

```
[inspect] test -f a.txt && echo 'a.txt exists' || echo 'a.txt not found'
    a.txt exists
[inspect] test -d backup/ && echo 'backup directory exists' || ...
    backup directory exists
[inspect] test -f backup/a.txt && echo 'Destination file exists' || ...
    Destination file not found
```

### 4. 翻譯和執行之間的時間差 —— 這是我們自己造成的洞

同一個輸入連跑兩次，產出的計劃在**安全性上是相反的**：

```sh
# 第一次：檢查寫進計劃裡
if [ ! -f a.txt ]; then echo "Error: a.txt not found" >&2; exit 1; fi
if [ ! -d backup/ ]; then ... fi
mv a.txt backup/

# 第二次：規劃時檢查完，交出一個裸的 mv
# Ensure source file exists
mv a.txt backup/a.txt
```

第二種有 TOCTOU 問題。而且**這個洞是「翻譯與執行分離」這個設計自己挖的**：
兩件事刻意拆開之後，中間隔多久完全不受控 —— 那份 `plan.sh` 可能三天後才被執行。
模型在規劃當下看到 `backup/a.txt` 不存在，不代表按下執行時它還不存在。

所以規則是：**`inspect` 的結論只能決定「要做什麼」，不能取代「執行當下能不能做」。**
檢查一定要寫成 `if` / `test` 擋在 script 裡，不能只留一行註解說我確認過了
（第 3 條那個「註解說謊」的現象，其實就是這個問題的前兆）。

寫進 system prompt 之後再跑兩次，兩次都把檢查寫進 script 了。

### 順帶學到的：產出不固定，是要設計的對象，不是瑕疵

note 講純工具時說「只是產出結果不固定」—— 上面那兩份計劃就是它的具體長相：
不是文字風格不同，是**一份安全、一份不安全**。

這代表提示詞裡沒明講的東西，模型會在兩種做法之間隨機擺盪；要哪一種就得寫死。
也代表這類工具沒辦法靠「跑一次看起來對」來驗收 —— 第一輪在 Windows 上也「看起來對」。
