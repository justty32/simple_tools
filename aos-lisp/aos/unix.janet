# unix.janet — 把一次 Unix 呼叫變成一個可以排進 queue 的 list。
#
# ★ 這是**擴充**，不是核心。`aos/runtime` 完全不認識 process；這個模組是
#   `(unix/install env)` 注入 env 的一項能力，不裝就沒有。
#
# ── 一次呼叫長這樣 ──────────────────────────────────────────────────
#
#     (unix/call
#       :argv   ["/bin/ls" "-la" "/tmp"]
#       :cwd    "/home/me"
#       :stdin  "/run/aos/calls/42/stdin"
#       :stdout "/run/aos/calls/42/stdout"
#       :stderr "/run/aos/calls/42/stderr"
#       :status "/run/aos/calls/42/status"
#       :caller "agent-7")            # 可選，不透明標籤
#
# 每個欄位都是字串或字串陣列，所以整個呼叫是**純資料**：寫得進檔案、讀得回來、
# marshal 得動、印出來人看得懂。三條串流不是把位元組塞在 form 裡，而是給位址，
# 由具體執行者去寫——這是這個設計最重要的一件事，見下面。
#
# ── ★ 為什麼三條串流走檔案，而不是管線 ──────────────────────────────
#
# aos-core 為了管線付出了很大代價：CLI 要開兩條執行緒（一條灌 stdin、一條收
# 輸出），daemon 端 stdin 要做成拉的，而且 README 裡那條「一定不要改成先 wait
# 再讀」的警告就是在講管線 buffer 塞滿之後兩邊互卡。
#
# 走檔案的話**這一整類問題不存在**：核心 loop 是一次跑一個、全程阻塞的，
# 如果三條串流是管線，一個輸出量大的子行程會塞滿 buffer 然後跟我們互等，
# 整台機器就停在那裡。檔案沒有這個問題，所以 `run` 可以老老實實 spawn 完就 wait。
#
# 代價是不能串流：要等它跑完才看得到完整輸出（雖然中途 `tail -f` 得到）。
#
# ── ★ exit status 為什麼不是「一個裝著整數的檔案」──────────────────
#
# 因為整數表達不了實際會發生的事：
#
#   1. 根本沒起來（執行檔不存在、沒權限）——那不是任何 exit code；
#   2. 被信號殺死——Unix 層面跟「退出碼 137」是**兩件事**；
#   3. 我們自己中途死掉——那是「不知道」，不是「失敗」。
#
# 而且「檔案不存在」必須能可靠地代表「還不知道」。這只有在寫入是**原子**的
# 時候才成立——寫一半的檔案被讀到就全毀了。所以：
#
#   * 內容是一張 Janet table 字面值（`parse` 讀得回來，零相依，不必拉 JSON）；
#   * 寫法是**先寫 `<status>.tmp`、再 `os/rename` 蓋過去**，同目錄的 rename 是原子的；
#   * 檔案在 = 結果已知；檔案不在 = 還不知道，**不可以當成失敗**。
#
# ── ★★ Janet 分不出 exit 137 和 SIGKILL ────────────────────────────
#
# 實測（見 test/unix.janet 有一項守著）：
#
#     sh -c 'exit 137'    os/proc-wait => 137
#     sh -c 'kill -9 $$'  os/proc-wait => 137
#
# `core/process` 身上只有 `:return-code` 和 `:pid`，沒有原始的 wait status，
# 所以 `WIFSIGNALED` 那個資訊在 Janet 這層就是拿不到。POSIX shell 也一樣
# （`$?` 同樣是 128+sig）。
#
# 照 AOS 的規矩「unknown 指證據不足，不能混成普通失敗」，所以不假裝知道：
#
#     {:status :exited     :code 7}                    # 0..127，確定正常退出
#     {:status :ambiguous  :code 137 :maybe-signal 9}  # 128..255，★ 分不出來
#     {:status :launch-error :message "..."}           # 根本沒起來
#
# 要把 `:ambiguous` 變成確定，得有人真的去看 `waitpid` 的 `WIFSIGNALED`——
# 那要 C。aos-core 的外掛 ABI 正好是這件事該待的地方。

(def kinds
  "status 檔裡 :status 欄位的三個值。"
  [:exited :ambiguous :launch-error])

(def required-paths
  "五個一定要給的路徑欄位。"
  [:cwd :stdin :stdout :stderr :status])

# ── 驗證：純函式，完全不碰檔案系統 ──────────────────────────────────

(defn- absolute? [s]
  (and (string? s) (string/has-prefix? "/" s)))

(defn- clean-string? [s]
  (and (string? s) (not (string/find "\0" s))))

(defn spec
  ``把呼叫的欄位驗成一張 call record。**純函式**：只看形狀，不碰檔案系統。
  不合法就丟錯，而且訊息會講清楚是哪個欄位。

  ★ argv[0] 一定要是絕對路徑，而且不走 shell、不走 PATH。
    走 PATH 的話「實際跑了哪支執行檔」取決於當下的環境變數，那不是證據。
    要 PATH 查找就自己先查好再送進來。

  ★ 五個路徑也一定要絕對路徑。因為 cwd 是**子行程的**（`os/spawn` 的 `:cd`），
    相對路徑會變成「相對於誰」的問題——是 daemon 的 cwd 還是子行程的？
    要求絕對路徑就沒有這個問題。

  :caller 是**可選的不透明標籤**，原樣記進 status 檔。不驗證、不 stat、
  也不保證唯一——它只回答「這次呼叫是誰掛的名」，不回答「這是不是同一個發起者」。

  :env 沒給就繼承本行程的環境變數；給了就是**完全換掉**（明確的環境政策）。``
  [opts]
  (def argv (get opts :argv))
  (unless (indexed? argv) (errorf ":argv 要是陣列，拿到 %q" (type argv)))
  (when (empty? argv) (error ":argv 不能是空的"))
  (each a argv
    (unless (clean-string? a)
      (errorf ":argv 每一項都要是不含 NUL 的字串，拿到 %q" a)))
  (unless (absolute? (first argv))
    (errorf ":argv[0] 要是絕對路徑（不走 shell、不走 PATH），拿到 %q" (first argv)))

  (def call @{:argv (map string argv)})
  (each k required-paths
    (def p (get opts k))
    (unless (clean-string? p) (errorf "%q 要是不含 NUL 的字串，拿到 %q" k p))
    (unless (absolute? p) (errorf "%q 要是絕對路徑，拿到 %q" k p))
    (put call k p))

  (when-let [c (get opts :caller)]
    (unless (clean-string? c) (errorf ":caller 要是不含 NUL 的字串，拿到 %q" c))
    (put call :caller c))

  (when-let [e (get opts :env)]
    (unless (dictionary? e) (errorf ":env 要是 table，拿到 %q" (type e)))
    (eachp [k v] e
      (unless (and (clean-string? k) (clean-string? v))
        (errorf ":env 的 key 與 value 都要是不含 NUL 的字串，拿到 %q => %q" k v)))
    (put call :env e))
  call)

# ── 落地前檢查：所有「會失敗」的事都要在有任何作用之前發生 ────────────

(defn- stat-kind [p]
  (when-let [s (os/stat p)] (s :mode)))

(defn- fresh-target?
  ``輸出目標可以寫嗎？

  已經存在的**普通檔案**要拒絕——那是上一次呼叫留下的證據，靜靜蓋掉的話
  就分不出「這次寫的」和「上次剩的」了。/dev/null 之類的裝置檔不受這條限制，
  而且這裡是看 mode 不是比對路徑字串，所以不必為 /dev/null 開特例。``
  [p]
  (not= (stat-kind p) :file))

(defn preflight
  ``碰檔案系統的檢查。過了才可以 spawn。

  ★ 這裡做完之前不可以有任何外部作用。理由是 AOS 那條規矩：意圖要在作用之前
    定下來，不能先做一半再發現參數是錯的。``
  [call]
  (unless (= (stat-kind (call :cwd)) :directory)
    (errorf ":cwd 不是目錄：%s" (call :cwd)))

  (unless (os/stat (call :stdin))
    (errorf ":stdin 檔案不存在：%s" (call :stdin)))

  (each k [:stdout :stderr :status]
    (unless (fresh-target? (call k))
      (errorf "%q 已經有一個普通檔案了，不覆蓋既有證據：%s" k (call k)))
    (def dir (string/join (slice (string/split "/" (call k)) 0 -2) "/"))
    (unless (= (stat-kind (if (empty? dir) "/" dir)) :directory)
      (errorf "%q 的所在目錄不存在：%s" k dir)))
  call)

# ── status 檔：原子寫入 ─────────────────────────────────────────────

(defn- serialize
  ``把紀錄變成一行 Janet 字面值。

  ⚠ `%q` 會把非 ASCII 轉成 `\xE4\xB8\xAD` 這種跳脫位元組，所以 `cat` 出來的
    中文路徑不好看。這是刻意的取捨：`%q` 的輸出 `parse` 回來位元組完全一致，
    而且不會有換行或引號跑出來破壞單行格式。自己拼字串去換取好看的話，
    就得自己處理跳脫——那是 aos-core 選長度前綴而不是 JSON 的同一個理由。``
  [record]
  (string/format "%q\n" record))

(defn- write-atomic
  ``先寫 `<path>.tmp` 再 rename 蓋過去。同目錄的 rename 是原子的，所以讀的人
  只會看到「還沒有」或「完整的一份」，不會看到寫到一半的。

  ⚠ 這擋得住「讀到半個檔」，**擋不住斷電**。Janet 沒有暴露 fsync
    （`(filter |(string/find "sync" $) (all-bindings root-env))` 是空的），
    `file/flush` 只到 libc buffer，不保證落到碟片。要斷電保證得往下走一層。``
  [path content]
  (def tmp (string path ".tmp"))
  (def f (file/open tmp :wn))
  (defer (file/close f)
    (file/write f content)
    (file/flush f))
  (os/rename tmp path))

(defn read-status
  ``讀回一個 status 檔。**檔案不存在回 nil——那是「還不知道」，不是失敗。**

  呼叫端一定要把這兩件事分開處理：nil 不可以當成「跑失敗了」，
  更不可以當成「沒跑過所以再跑一次」。``
  [path]
  (when (os/stat path)
    (parse (slurp path))))

# ── 執行 ────────────────────────────────────────────────────────────

(defn- classify
  "把 os/proc-wait 的回傳值翻成一個誠實的結論。"
  [code]
  (if (< code 128)
    @{:status :exited :code code}
    # ★ 128..255 分不出「程式自己 exit 這個碼」和「被 (- code 128) 號信號殺死」。
    #   見本檔開頭。不猜，標成 :ambiguous。
    @{:status :ambiguous :code code :maybe-signal (- code 128)}))

(defn run
  ``真的跑一次。回傳結論紀錄，同時把它寫進 :status 檔。

  ── 動作順序（順序是有意義的）────────────────────────────────────
    1. spec      純驗證，還沒有任何作用
    2. preflight 檔案系統檢查，還沒有任何作用
    3. 開 stdout／stderr（截斷建立）── ★ 第一個外部作用
    4. spawn
    5. wait
    6. 原子寫 status

  ★ 第 3 步同時是一個免費的兩階段標記：status 檔不在的時候，
    **stdout 檔在不在**就分得出「根本沒起來」和「可能跑過了」——
    後者絕對不可以自動重跑。

  nonzero、信號、起不來都是**結果**，回傳資料並寫 status 檔。
  只有「我們自己沒能問出結果」才會丟錯（那時候 status 檔不會存在）。``
  [opts]
  (def call (spec opts))
  (preflight call)

  (def started (os/time))
  (def t0 (os/clock :monotonic))

  # 第一個外部作用。:wn = 寫入 + 開不起來就丟錯（不加 n 的話失敗是回 nil）。
  (def out (file/open (call :stdout) :wn))
  (def err (file/open (call :stderr) :wn))
  (def in (file/open (call :stdin) :rn))

  (def child @{:in in :out out :err err :cd (call :cwd)})
  # 給了 :env 就用 :e 旗標**整套換掉**；沒給就繼承本行程的。
  # ⚠ 只寫 :e 而不給 table，或給了 table 卻漏了 :e，都會被安靜忽略。
  (def flags (if (call :env) :e (keyword "")))
  (when-let [e (call :env)] (merge-into child e))

  (def result
    (try
      (let [proc (os/spawn (call :argv) flags child)
            pid (proc :pid)
            code (os/proc-wait proc)]
        (merge (classify code) @{:pid pid}))
      ([e] @{:status :launch-error :message (string e)})))

  (each f [in out err] (file/close f))

  (def record
    (merge result
           @{:argv (call :argv)
             :cwd (call :cwd)
             :caller (call :caller)   # 可選的不透明標籤，原樣帶著
             :started started
             :duration (- (os/clock :monotonic) t0)
             :stdout-size (get (os/stat (call :stdout)) :size)
             :stderr-size (get (os/stat (call :stderr)) :size)}))

  (write-atomic (call :status) (serialize record))
  record)

# ── 注入 ────────────────────────────────────────────────────────────

(defn install
  ``把 unix/* 注入一張 env。核心不會自己做這件事——要有 process 能力就明講。

      (import ./unix :as unix)
      (def rt (runtime/new))
      (unix/install (rt :env))``
  [env]
  (put env 'unix/call @{:value (fn [& kvs] (run (struct ;kvs)))
                        :doc "(unix/call :argv [...] :cwd .. :stdin .. :stdout .. :stderr .. :status ..) 跑一次 Unix 呼叫"})
  (put env 'unix/spec @{:value (fn [& kvs] (spec (struct ;kvs)))
                        :doc "(unix/spec :argv [...] …) 只驗證不執行，回傳 call record"})
  (put env 'unix/read-status @{:value read-status
                               :doc "(unix/read-status path) 讀回 status 檔；不存在回 nil = 還不知道"})
  env)
