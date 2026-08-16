# 把一次 Unix 呼叫變成一個 list。

(import ../aos/unix :as unix)
(import ../aos/runtime :as runtime)

(defn threw? [f] (= :threw (try (do (f) :no) ([_] :threw))))

# 每個案例一個乾淨目錄，跑完不留。
(def root (string "/tmp/aos-lisp-unix-" (os/time) "-" (os/getpid)))
(os/mkdir root)
(var seq 0)
(defn workspace
  "開一個案例目錄，回傳一張已經填好五個路徑的 opts。stdin 內容給 in。"
  [&opt in]
  (default in "")
  (set seq (inc seq))
  (def d (string root "/" seq))
  (os/mkdir d)
  (spit (string d "/stdin") in)
  @{:cwd d
    :stdin (string d "/stdin")
    :stdout (string d "/stdout")
    :stderr (string d "/stderr")
    :status (string d "/status")})

# ── spec：純驗證，不碰檔案系統 ──────────────────────────────────────
(def w (workspace))

(def ok (unix/spec (merge w @{:argv ["/bin/echo" "hi"]})))
(assert (deep= (ok :argv) @["/bin/echo" "hi"]) "argv 留著")
(assert (= (ok :cwd) (w :cwd)) "五個路徑都留著")

(assert (threw? |(unix/spec (merge w @{:argv []}))) "空 argv 拒絕")
(assert (threw? |(unix/spec (merge w @{:argv "ls"}))) "argv 不是陣列就拒絕")
(assert (threw? |(unix/spec (merge w @{:argv ["ls"]})))
        "★ argv[0] 相對路徑要拒絕——走 PATH 的話跑了哪支執行檔不是證據")
(assert (threw? |(unix/spec (merge w @{:argv ["/bin/echo" 42]}))) "argv 非字串拒絕")
(assert (threw? |(unix/spec (merge w @{:argv ["/bin/echo" "a\0b"]})))
        "★ argv 含 NUL 拒絕——execve 帶不過去")
(each k [:cwd :stdin :stdout :stderr :status]
  (assert (threw? |(unix/spec (merge w @{:argv ["/bin/echo"]} (table k "relative/path"))))
          (string/format "%q 相對路徑要拒絕" k))
  # ⚠ 不能寫 (table k nil)——那是**空 table**，merge 進去等於沒動。要真的拿掉 key。
  (def missing (merge w @{:argv ["/bin/echo"]}))
  (put missing k nil)
  (assert (threw? |(unix/spec missing)) (string/format "%q 沒給要拒絕" k)))
(assert (threw? |(unix/spec (merge w @{:argv ["/bin/echo"] :env "PATH=/bin"}))) ":env 不是 table 拒絕")

# spec 真的沒碰檔案系統：路徑指向不存在的東西也照過
(assert (unix/spec @{:argv ["/bin/echo"] :cwd "/no/such"
                     :stdin "/no/such" :stdout "/no/such" :stderr "/no/such" :status "/no/such"})
        "★ spec 是純函式，不存在的路徑不歸它管")

# :caller 是可選的不透明標籤：不給可以，給了不驗證
(assert (nil? ((unix/spec (merge w @{:argv ["/bin/echo"]})) :caller)) ":caller 可以不給")
(assert (= ((unix/spec (merge w @{:argv ["/bin/echo"] :caller "agent-7"})) :caller) "agent-7")
        "★ 給了就原樣留著，不必是路徑、不檢查存不存在")
(assert (threw? |(unix/spec (merge w @{:argv ["/bin/echo"] :caller 42}))) ":caller 非字串拒絕")

# ── preflight：檔案系統檢查 ─────────────────────────────────────────
(assert (threw? |(unix/run (merge (workspace) @{:argv ["/bin/echo"] :cwd "/no/such/dir"})))
        "cwd 不是目錄要拒絕")
(assert (threw? |(unix/run (merge (workspace) @{:argv ["/bin/echo"] :stdin "/no/such/stdin"})))
        "stdin 不存在要拒絕")
(assert (threw? |(unix/run (merge (workspace) @{:argv ["/bin/echo"] :stdout "/no/such/dir/out"})))
        "輸出目錄不存在要拒絕")

# ★ 不覆蓋既有證據
(def w-dirty (workspace))
(spit (w-dirty :status) "上一次留下的")
(assert (threw? |(unix/run (merge w-dirty @{:argv ["/bin/echo"]})))
        "★ status 檔已經有東西就拒絕，不靜靜蓋掉上一次的證據")
(assert (= (string (slurp (w-dirty :status))) "上一次留下的") "而且真的沒動它")

# 但裝置檔不算既有證據（不必為 /dev/null 開特例，是看 mode 不是比對路徑）
(def w-null (workspace))
(def r-null (unix/run (merge w-null @{:argv ["/bin/echo" "丟掉"] :stdout "/dev/null"})))
(assert (= (r-null :status) :exited) "★ /dev/null 當輸出可以，因為它不是普通檔案")

# ── 跑成功 ──────────────────────────────────────────────────────────
(def w1 (workspace))
(def r1 (unix/run (merge w1 @{:argv ["/bin/sh" "-c" "echo out; echo err >&2; exit 0"]})))
(assert (= (r1 :status) :exited) ":exited")
(assert (= (r1 :code) 0) "code 0")
(assert (number? (r1 :pid)) "pid 記下來了")
(assert (= (string (slurp (w1 :stdout))) "out\n") "stdout 寫進指定的檔")
(assert (= (string (slurp (w1 :stderr))) "err\n") "stderr 寫進指定的檔")
(assert (= (r1 :stdout-size) 4) "紀錄裡有 stdout 大小")

# status 檔讀得回來，而且跟回傳值一致
(def back (unix/read-status (w1 :status)))
(assert (= (back :status) :exited) "status 檔讀得回來")
(assert (= (back :code) 0) "code 一致")
(assert (deep= (freeze (r1 :argv)) (freeze (back :argv))) "★ status 檔自帶 argv，是自足的證據")
(assert (nil? (os/stat (string (w1 :status) ".tmp"))) "★ 原子寫入的 .tmp 沒有留下來")

# ── stdin 真的餵進去了 ──────────────────────────────────────────────
(def w2 (workspace "餵進去的內容\n"))
(unix/run (merge w2 @{:argv ["/bin/cat"]}))
(assert (= (string (slurp (w2 :stdout))) "餵進去的內容\n") "★ stdin 從指定的檔讀進子行程")

# ── cwd 真的生效，而且沒有動到本行程 ────────────────────────────────
(def before-cwd (os/cwd))
(def w3 (workspace))
(unix/run (merge w3 @{:argv ["/bin/pwd"]}))
(assert (= (string/trim (slurp (w3 :stdout))) (w3 :cwd)) "★ 子行程的 cwd 是指定的那個")
(assert (= (os/cwd) before-cwd) "★ 而且沒有改到 runtime 自己的 cwd（:cd 不是 os/cd）")

# ── nonzero 是結果，不是錯誤 ────────────────────────────────────────
(def w4 (workspace))
(def r4 (unix/run (merge w4 @{:argv ["/bin/sh" "-c" "exit 7"]})))
(assert (= (r4 :status) :exited) "★ nonzero 也是 :exited，不是丟錯")
(assert (= (r4 :code) 7) "碼是 7")
(assert (= ((unix/read-status (w4 :status)) :code) 7) "status 檔也寫了")

# ── ★★ 本檔最重要的一項：Janet 分不出 exit 137 和 SIGKILL ──────────
#
# 兩邊 os/proc-wait 都回 137，core/process 身上也沒有原始 wait status。
# 所以這兩個案例的結論**必須一模一樣**——如果哪天有人「修好」成不一樣，
# 那一定是在猜，不是在讀證據。
(def w5 (workspace))
(def r5 (unix/run (merge w5 @{:argv ["/bin/sh" "-c" "exit 137"]})))
(def w6 (workspace))
(def r6 (unix/run (merge w6 @{:argv ["/bin/sh" "-c" "kill -9 $$"]})))
(assert (= (r5 :status) :ambiguous) "★ exit 137 標成 :ambiguous")
(assert (= (r6 :status) :ambiguous) "★ 被 SIGKILL 也標成 :ambiguous")
(assert (= (r5 :code) (r6 :code) 137) "兩邊都是 137")
(assert (= (r5 :maybe-signal) (r6 :maybe-signal) 9) "只能說「可能是 9 號信號」")
(assert (= (r5 :status) (r6 :status))
        "★★ 這兩個在 Janet 這層無法區分——結論一樣才是誠實的")

# 127 以下不標 ambiguous
(def w7 (workspace))
(assert (= ((unix/run (merge w7 @{:argv ["/bin/sh" "-c" "exit 127"]})) :status) :exited)
        "127 還是確定的 :exited")

# ── 起不來也是結果 ──────────────────────────────────────────────────
(def w8 (workspace))
(def r8 (unix/run (merge w8 @{:argv ["/no/such/binary-xyz"]})))
(assert (= (r8 :status) :launch-error) "★ 起不來是 :launch-error，不是丟錯")
(assert (string? (r8 :message)) "有原因")
(assert (= ((unix/read-status (w8 :status)) :status) :launch-error) "★ 起不來也留下 status 檔")
(assert (= (string (slurp (w8 :stdout))) "") "stdout 檔建起來了但是空的")

# ── ★ 兩階段標記：status 不在的時候，stdout 在不在分得出兩件事 ──────
(def w9 (workspace))
(assert (nil? (unix/read-status (w9 :status))) "★ 還沒跑：status 檔不存在 = 還不知道")
(assert (nil? (os/stat (w9 :stdout))) "★ 而且 stdout 也不在 = 確定根本沒起來")
(unix/run (merge w9 @{:argv ["/bin/echo" "x"]}))
(assert (truthy? (os/stat (w9 :stdout))) "跑過之後 stdout 檔在")
(assert (truthy? (unix/read-status (w9 :status))) "status 也在")

# ── caller 標籤原樣進 status 檔 ─────────────────────────────────────
(def w10 (workspace))
(def r10 (unix/run (merge w10 @{:argv ["/bin/echo" "x"] :caller "agent-7"})))
(assert (= (r10 :caller) "agent-7") "回傳值帶著標籤")
(assert (= ((unix/read-status (w10 :status)) :caller) "agent-7") "status 檔也帶著")

(def w11 (workspace))
(def r11 (unix/run (merge w11 @{:argv ["/bin/echo" "x"]})))
(assert (nil? (r11 :caller)) "沒給就是 nil，不是必填")

# ── env 政策 ────────────────────────────────────────────────────────
(def w12 (workspace))
(os/setenv "AOS_TEST_LEAK" "leaked")
(unix/run (merge w12 @{:argv ["/bin/sh" "-c" "echo ${AOS_TEST_LEAK:-none}"]}))
(assert (= (string/trim (slurp (w12 :stdout))) "leaked") "沒給 :env 就繼承本行程的")

(def w13 (workspace))
(unix/run (merge w13 @{:argv ["/bin/sh" "-c" "echo ${AOS_TEST_LEAK:-none}"]
                       :env @{"PATH" "/bin"}}))
(assert (= (string/trim (slurp (w13 :stdout))) "none")
        "★ 給了 :env 就是整套換掉，不是疊上去")
(os/setenv "AOS_TEST_LEAK" nil)

# ── read-status 的語義 ──────────────────────────────────────────────
(assert (nil? (unix/read-status "/no/such/status"))
        "★ 檔案不存在回 nil = 還不知道，不可以當成失敗")

# ── 整合：一次呼叫就是一個排得進 queue 的 list ──────────────────────
(def rt (runtime/new))
(unix/install (rt :env))

(def w14 (workspace))
(def t (runtime/submit rt
         ~(unix/call :argv ["/bin/sh" "-c" "echo 從任務裡跑的"]
                     :cwd ,(w14 :cwd)
                     :caller "agent-7"
                     :stdin ,(w14 :stdin)
                     :stdout ,(w14 :stdout)
                     :stderr ,(w14 :stderr)
                     :status ,(w14 :status))))
(runtime/drain rt)
(assert (= (t :state) :done) "★ 任務跑完了")
(assert (= (get (t :value) :status) :exited) "任務的值就是那張結論紀錄")
(assert (= (string (slurp (w14 :stdout))) "從任務裡跑的\n") "★ 輸出真的落到指定的檔")

# 壞呼叫在任務裡是 :failed，炸不掉 loop
(def bad (runtime/submit rt '(unix/call :argv ["relative"] :cwd "/tmp"
                                        :stdin "/tmp" :stdout "/tmp" :stderr "/tmp" :status "/tmp")))
(def after (runtime/submit rt '(+ 1 1)))
(runtime/drain rt)
(assert (= (bad :state) :failed) "★ 驗證不過是 :failed")
(assert (= (after :state) :done) "★ 而且下一個任務照跑")

# 核心本身沒有 process 能力：沒 install 就沒有
(def rt2 (runtime/new))
(def no-cap (runtime/submit rt2 '(unix/call :argv ["/bin/echo"])))
(runtime/drain rt2)
(assert (= (no-cap :state) :failed) "★ 沒 install 的 env 裡沒有 unix/call——核心不認識 process")

# 收尾
(defn rm-rf [p]
  (if (= (os/stat p :mode) :directory)
    (do (each e (os/dir p) (rm-rf (string p "/" e))) (os/rmdir p))
    (os/rm p)))
(rm-rf root)

(print "unix ✓")
