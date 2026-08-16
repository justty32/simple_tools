# 求值層。★ 這裡守著 sigmask 那個坑。

(import ../aos/evaluate :as evaluate)

(def e (make-env))

# 正常
(def ok (evaluate/in-env e '(+ 1 2)))
(assert (= (ok :status) :ok) ":ok")
(assert (= (ok :value) 3) "值")

# 定義落在給的那張 env，不污染這裡
(evaluate/in-env e '(def secret 7))
(assert (truthy? (get e 'secret)) "def 落在給的 env")
(assert (nil? (get (curenv) 'secret)) "★ 沒有污染呼叫端的 env")
(assert (= ((evaluate/in-env e '(* secret 6)) :value) 42) "跨次求值看得到前面定義的")

# 三種出錯都變成資料，不會丟出來
(def boom (evaluate/in-env e '(error :boom)))
(assert (= (boom :status) :error) "error 變成 :status :error")
(assert (= (boom :error) :boom) "★ 錯誤值原樣留著，不是被 string 化")
(assert (string/find "boom" (boom :trace)) "有堆疊字串")
(assert (not (string/find "\e[" (boom :trace))) "堆疊沒有 ANSI 色碼")

(def unknown (evaluate/in-env e '(no-such-fn 1)))
(assert (= (unknown :status) :error) "編譯期的未知 symbol 也是 :error")

(def bad-call (evaluate/in-env e '(1 2 3)))
(assert (= (bad-call :status) :error) "呼叫不是函式的東西也是 :error")

(def runtime-err (evaluate/in-env e '(+ 1 "x")))
(assert (= (runtime-err :status) :error) "型別錯也是 :error")

# yield
(def paused (evaluate/in-env e '(do (yield :half) :whole)))
(assert (= (paused :status) :paused) "yield => :paused")
(assert (= (paused :value) :half) "yield 出來的值")
(assert (fiber? (paused :fiber)) "fiber 留著")

(def resumed (evaluate/resume-fiber (paused :fiber)))
(assert (= (resumed :status) :ok) "接回去跑完")
(assert (= (resumed :value) :whole) "後半段的值")

# resume 可以送值回去給 yield
(def p2 (evaluate/in-env e '(let [got (yield :ask)] [:got got])))
(def r2 (evaluate/resume-fiber (p2 :fiber) :answer))
(assert (deep= (r2 :value) [:got :answer]) "送回去的值被 yield 收到")

# 使用者信號 0-7 收得到
(def sig (evaluate/in-env e '(signal 3 :hi)))
(assert (= (sig :status) :signal) "user3 => :signal")
(assert (= (sig :signal) :user3) "信號種類")
(assert (= (sig :value) :hi) "帶的值")

# ★★★ 本檔最重要的一項：任務裡做真 IO 一定要能跑完。
#
# sigmask 若改成 :a，下面會拿到 :suspended / nil 而不是 :ok / :slept——
# 而且不會有任何錯誤訊息，任務就那樣永遠停著。原因是 ev 用 user9(await)
# 實作阻塞 IO，:a 把它一起擋掉了。
(def t0 (os/clock :monotonic))
(def io (evaluate/in-env e '(do (ev/sleep 0.05) :slept)))
(def elapsed (- (os/clock :monotonic) t0))
(assert (= (io :status) :ok) "★ 任務裡的 ev/sleep 要能完成（sigmask 不可以是 :a）")
(assert (= (io :value) :slept) "★ 而且拿得到值")
(assert (>= elapsed 0.04) "★ 真的睡了，不是立刻回來")

# 任務裡開子 fiber / 併發也要能動
(def gathered (evaluate/in-env e '(ev/gather (do (ev/sleep 0.01) :a) (do (ev/sleep 0.02) :b))))
(assert (= (gathered :status) :ok) "任務裡的 ev/gather 能完成")
(assert (deep= (gathered :value) @[:a :b]) "ev/gather 的結果")

# sigmask 這個常數本身：不可以含 8 或 9
(def m (string evaluate/sigmask))
(assert (not (string/find "8" m)) "★ sigmask 不可以擋 user8（中斷）")
(assert (not (string/find "9" m)) "★ sigmask 不可以擋 user9（await），否則 IO 全死")
(assert (not (string/find "a" m)) "★ sigmask 不可以是 :a")
(assert (string/find "e" m) "sigmask 要擋錯誤")
(assert (string/find "y" m) "sigmask 要擋 yield")

(print "evaluate ✓")
