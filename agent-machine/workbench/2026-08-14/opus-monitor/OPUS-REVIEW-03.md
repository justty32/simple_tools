# OPUS-REVIEW-03：候選方案取捨

## 結論

生命週期與成長路線的候選大致可採，直接往下做即可。真正要動的有三處：**「一個地盤一個主事者」若照字面實作成每個 Task 各自搶寫入權會死鎖**；**入口與「哪些檔案算 Definition」被當成二選一**；**`paused-safe` 有一條自己頁面就已否定的條件**。另外有三項候選在花原型預算但買不到資訊，建議直接刪。

## 保留

- Task 生命週期方案三（安全落盤接受後固定 `task_id`）＋內部 attempt（方案四）。這是四案中唯一讓排隊、暫停、重啟、查完成結果都只用一個 ID 的。
- 「三種成長不要混在一起」（單檔變資料夾／拆成多個 Functions／升成 child agent）。這是整份 ledger 最有價值的一段。
- 記憶採混合模式（FILESYSTEM 方案四），以及「不要只寫一個模糊的 `memory_mode`」。
- 不猜入口、不搜尋 `$PATH`、接受時保存已解析入口。
- 「有 intent 沒有完整 Receipt 就是結果不明，不自動重送」。
- C++ 掌管會改變正式狀態的硬邊界、Janet 只讀已驗證快照並回建議。
- 普通檔案是權威，SQLite 之後再用同一組測試比較。

## 修改

1. **並行政策合併成一條租約規則**。寫入權應綁 agent root 且以**整棵 Task tree** 為持有單位，child 直接繼承。反例：`T1 (ask)` 持有 `bot-a`，呼叫 `./ask/functions/apply` 產生 `T2`，`T2` 排隊等寫入權、`T1` 等 `T2`，永遠不會結束。目前兩頁都沒處理這件事。
2. **入口與 Definition 範圍是兩個問題，不是 A 對 B**。固定檔名 `call` 回答「怎麼跑」；入口設定檔（manifest）回答「哪些檔案算 Definition」。`ask/` 底下放了 `lib/render.py`，若版本只 hash `call`，改 helper 不換版，兩個 Task 會宣稱同版程式卻行為不同。所以有 helper 就需要 manifest，沒有時可以不寫。
3. **`paused-safe` 的「受管理的 process tree 已空」不可驗證**。同一頁自己就寫了「process 結束不代表它 fork 出去的 background child 已結束」。最小修正：條件降為「AOS 直接管理的 process 已回收，且寫入權已釋放」，並在 checkpoint 上標明背景孫代無法保證。這條已列入待驗，但**條件的措辭要先改**，否則原型會照著一個做不到的定義去驗。
4. **三種 generation 砍成兩種**。`runtime_generation`（這次 AOS 啟動）不是版本，是租約的一個欄位；留成第三個名詞只會讓人以為它和 Definition／memory 同級。
5. **「唯讀互動」要有可驗證定義才能寫進政策**。可行的最小定義是「宣告唯讀＋不授寫入權＋不得發布記憶」，並明說這不是 sandbox：`./ask --dry-run` 偷偷寄信，AOS 擋不住。

## 捨棄

- **方案一（Task 隨 PID 生滅）作為公開模型**。頁面已建議只作內部觀察，請直接標成捨棄，不要留在四選一裡讓人以為還要比。
- **方案二 vs 方案三的比較原型**（TASK-LIFECYCLE「建議順序」第 1 點）。這一點與同頁結論矛盾：頁面已列出雙 ID 的全部成本（升格點 crash 要維護兩套連結、CLI 與 relation 都要處理），也已推薦方案三。再花一輪原型去量已知的東西不划算，把預算移給租約與 accept crash。
- **「四種接法等量小原型」**（CPP-JANET 階段 6）。在語意還沒定案前做四倍工作。只做兩個 process 用固定 JSON 那一案；內嵌與 FFI 等 C++ 證據層穩定後再談。

## 延後

SQLite 權威與衍生 index；branch／overlay；path 搬移與跨機器；Step 原子邊界的精確定義；`.aos` 的「中央 store 統一保存」——延後但**保留為替代方案**，因為它是唯一能在多 agent 運維下把單一 writer 做簡單的方案。

## 這些只是候選，不可寫成使用者決定

Call 必須不可修改、Receipt、事件格式、attempt、各種版本號、Task 從接受一路延續到完成紀錄、同一 Call 可產生多個 Task、固定檔名 `call`——全部是內部候選。連「不要猜入口」這條限制本身，OPEN-QUESTIONS 也註明尚未請使用者裁決。

使用者實際說過的只有：正在執行中的 Function 實例叫 Task；資料夾可以承載 Function definition；agent root 的絕對路徑是 ID 與地盤；避免同一 agent 多實例；偏好 `.aos`；agent root 要能直接上 GitHub。往上寫的每一層都要標來源。

## 衝突、重複與不必要的複雜

- 「一個地盤一個主事者」在兩頁各講一次且不一致：OPEN-QUESTIONS 第 5 題是四選一（排隊／拒絕／唯讀／無副作用並行），FILESYSTEM 方案四則直接寫「同一 root 一次只有一棵可寫 Task tree」。後者已經是答案，前者應收斂掉。
- 入口四案其實只有兩個決策點。方案 C（薄 launcher＋module 目錄）與方案 D（`ask.aos` manifest）都是「一個 Function 兩條路徑」的變形，兩頁自己也都指出這個缺點，可以直接淘汰，只留固定 `call` 與 manifest。
- 來源集不自足：OPEN-QUESTIONS 指向 `DURABILITY-CANDIDATES.md` 與 `CLI-CANDIDATES.md`，但本輪明確不可讀。若那兩頁有相反決定，本回覆不涵蓋。

## 會推翻推薦案的反例與最小修正

1. **child 繼承寫入權 → 無法單獨取消某個 child。** 最小修正：取消與 pause 都以 tree 為單位，child 只能停在邊界；只有真的需要單獨取消時，才值得引入 per-child 寫入權。
2. **live root ＋ 唯讀並行 → 唯讀可能讀到半份暫存。** 最小修正：記憶只在發布點原子換版，唯讀一律讀已發布版本，不看暫存區。
3. **`.aos/local/` 被誤 commit → clone 帶著別台機器的租約。** 最小修正：啟動時比對租約內的機器與啟動世代，不符就忽略並警告，而不是相信它。
4. **預設排隊 → 一個壞掉的長工作把後面全卡住。** 最小修正：排隊要在 `aos task list` 可見、可明確取消即可，不需要自動逾時（自動逾時會把「結果不明不重送」的原則打破）。

## 給主 agent 的優先處理清單

**現在做原型**

1. 寫入權租約：child 繼承、外來可寫排隊、唯讀不排隊、Ctrl-C 不釋放。最高優先，因為死鎖反例會直接推翻目前的並行敘述。
2. 落盤接受即發 `task_id`；accept 後、dispatch 前殺掉 manager，驗 ID 不變且外部作用 0 次。
3. `ask` → `ask/call` 命令不變；改 message 不換版、改宣告的 helper 換版；放假的 `main.py` 不被跑。
4. `.aos` 切成可攜與本機兩半；clone 後零自動執行；舊啟動世代的執行中 Task 一律不自動續跑。
5. C++ 只重驗 Linux 邊界：fd 與檔案鎖是否被子代繼承、fsync／rename／目錄 fsync 次序、torn tail。這些 Python 測不出來。

**先不做**

6. 方案二（雙 ID）比較原型——直接採方案三。
7. 四種 C++／Janet 接法等量原型——只做兩個 process 那一案，且先過 Linux Janet 工具鏈檢查。
8. SQLite、branch／overlay、跨機器、path 搬移。
