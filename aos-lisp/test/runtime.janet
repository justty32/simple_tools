# 核心 loop 本體。

(import ../aos/runtime :as runtime)
(import ../aos/task :as task)

(defn threw?
  "f 有沒有丟錯。用來斷言「這件事必須失敗」。"
  [f]
  (= :threw (try (do (f) :no) ([_] :threw))))

# ── 最短的一輪 ──────────────────────────────────────────────────────
(def rt (runtime/new))
(def t1 (runtime/submit rt '(+ 1 2)))
(assert (= (t1 :state) :queued) "submit 之後是 :queued")
(assert (= (t1 :id) 1) "id 從 1 開始")
(assert (= (get (runtime/status rt) :queued) 1) "queue 裡有一個")

(runtime/step rt)
(assert (= (t1 :state) :done) "跑完了")
(assert (= (t1 :value) 3) "值")
(assert (= (get (runtime/status rt) :handled) 1) "handled 加一")
(assert (nil? (runtime/step rt)) "空的 step 回 nil，不擋")
(assert (= (runtime/task-by-id rt 1) t1) "查得到同一個 task 參考")

# ── ★ 長存 env：跨任務累積 ──────────────────────────────────────────
(runtime/submit rt '(def counter 0))
(runtime/submit rt '(set counter 1))   # 會失敗：counter 是 def 不是 var
(runtime/submit rt '(def other 5))
(runtime/submit rt '(+ counter other))
(def ran (runtime/drain rt))
(assert (= (length ran) 4) "drain 把排著的都做完")
(assert (= ((last ran) :value) 5) "★ 後面的任務看得到前面任務定義的東西")
(assert (= ((ran 1) :state) :failed) "set 一個 def 會失敗")
(assert (= ((ran 2) :state) :done) "★ 但下一個任務照跑，不受前一個失敗影響")

# host 自己的 env 沒有被污染
(assert (nil? (get (curenv) 'counter)) "★ 任務的 def 沒有跑到 host 這裡來")

# ── 一個爛任務炸不掉 loop ───────────────────────────────────────────
(def rt2 (runtime/new))
(runtime/submit rt2 '(error "boom"))
(runtime/submit rt2 '(no-such-symbol))
(runtime/submit rt2 '(1 2 3))
(runtime/submit rt2 '(+ 40 2))
(runtime/stop rt2)
(runtime/run rt2)                       # ★ 這行沒炸就是重點
(def all (runtime/tasks rt2))
(assert (= (length all) 4) "四個都有紀錄")
(assert (= ((last all) :state) :done) "★ 前面全爛，最後一個照樣跑完")
(assert (= ((last all) :value) 42) "而且值是對的")
(assert (string? ((all 0) :trace)) "失敗的留著堆疊")
(assert (not (get (runtime/status rt2) :running)) "run 回來之後 :running 是 false")

# ── stop 的語義：排在前面的做完，之後不收 ───────────────────────────
(def rt3 (runtime/new))
(runtime/submit rt3 '(def a 1))
(runtime/submit rt3 '(def b 2))
(runtime/stop rt3)
(assert (threw? |(runtime/submit rt3 '(def c 3))) "★ 收工之後 submit 丟錯，不安靜吞掉")
(assert (= (length (runtime/tasks rt3)) 2) "★ 被拒絕的任務不會留在任務表裡")
(runtime/run rt3)                       # ★ 靠哨兵回來，不會永遠擋著
(assert (= (get (runtime/status rt3) :handled) 2) "★ close 之前排的兩個都做完了")

# ── on-result ───────────────────────────────────────────────────────
(def seen @[])
(def rt4 (runtime/new @{:on-result (fn [_ t] (array/push seen [(t :id) (t :state)]))}))
(runtime/submit rt4 '(+ 1 1))
(runtime/submit rt4 '(error :x))
(runtime/drain rt4)
(assert (deep= seen @[[1 :done] [2 :failed]]) "★ on-result 成功失敗都會叫")

# ── aos/* 內建 ──────────────────────────────────────────────────────
(def rt5 (runtime/new))

# 任務裡再排任務：新的排在隊尾，同一輪 drain 就會跑到
(runtime/submit rt5 '(aos/submit '(def spawned :yes)))
(def r5 (runtime/drain rt5))
(assert (= (length r5) 2) "★ 任務排出來的任務同一輪就跑掉了")
(assert (= ((r5 1) :value) :yes) "而且真的跑了")
(assert (= ((r5 0) :value) (r5 1)) "aos/submit 回傳的就是那個 task")

# aos/self 是正在跑的那個
(def st (runtime/submit rt5 '(get (aos/self) :id)))
(runtime/drain rt5)
(assert (= (st :value) (st :id)) "★ aos/self 就是自己")
(assert (nil? (rt5 :current)) "跑完之後 :current 清掉")

# aos/status / aos/tasks / aos/task
(def stat (runtime/submit rt5 '(get (aos/status) :handled)))
(runtime/drain rt5)
(assert (number? (stat :value)) "aos/status 拿得到數字")

(def lst (runtime/submit rt5 '(length (aos/tasks))))
(runtime/drain rt5)
(assert (= (lst :value) (lst :id)) "aos/tasks 看得到目前所有任務")

(def one (runtime/submit rt5 '(get (aos/task 1) :state)))
(runtime/drain rt5)
(assert (= (one :value) :done) "aos/task 查得到別的任務")

# aos/env：任務自省自己那張表
(def intro (runtime/submit rt5 '(truthy? (find |(= $ 'spawned) (all-bindings (aos/env) true)))))
(runtime/drain rt5)
(assert (= (intro :value) true) "★ 任務看得到自己那張 env 裡有什麼")

# aos/stop：任務可以叫整台收工
(def rt6 (runtime/new))
(runtime/submit rt6 '(def before :ran))
(runtime/submit rt6 '(aos/stop))
(runtime/submit rt6 '(def after :also-ran))
(runtime/run rt6)                       # ★ 靠任務裡的 aos/stop 回來
(assert (= (get (runtime/status rt6) :handled) 3) "★ stop 之前排的都做完了（含 stop 後面那個）")
(assert (get (runtime/status rt6) :closed) "關了")

# ── yield：:paused ＋ resume ────────────────────────────────────────
(def rt7 (runtime/new))
(def y (runtime/submit rt7 '(let [got (yield :half)] [:whole got])))
(runtime/drain rt7)
(assert (= (y :state) :paused) "★ yield 的任務停在 :paused，loop 沒被它卡住")
(assert (= (y :value) :half) "yield 出來的值")
(assert (fiber? (y :fiber)) "fiber 留著")

# loop 繼續跑別的
(runtime/submit rt7 '(+ 1 1))
(runtime/drain rt7)
(assert (= (get (runtime/status rt7) :handled) 2) "★ 有任務停在 :paused，後面的照跑")

(runtime/resume-task rt7 (y :id) :sent)
(assert (= (y :state) :done) "接回去跑完了")
(assert (deep= (y :value) [:whole :sent]) "送回去的值收得到")
(assert (threw? |(runtime/resume-task rt7 (y :id))) "★ 已經 :done 的接不回去")
(assert (threw? |(runtime/resume-task rt7 999)) "不存在的 id 丟錯")

# ── 任務裡做真 IO ───────────────────────────────────────────────────
(def rt8 (runtime/new))
(def io (runtime/submit rt8 '(do (ev/sleep 0.02) :slept)))
(runtime/drain rt8)
(assert (= (io :state) :done) "★ 任務裡的阻塞 IO 能跑完（sigmask 對）")
(assert (= (io :value) :slept) "值")

# ── 自己給 env：aos/* 不注入 ────────────────────────────────────────
(def plain (make-env))
(def rt9 (runtime/new @{:env plain}))
(def no-builtin (runtime/submit rt9 '(aos/status)))
(runtime/drain rt9)
(assert (= (no-builtin :state) :failed) "★ 自己給的 env 不會被塞 aos/*")
(runtime/submit rt9 '(def in-my-env 1))
(runtime/drain rt9)
(assert (truthy? (get plain 'in-my-env)) "定義落在我給的那張表")

# ── env 不是單例：兩台各自獨立 ──────────────────────────────────────
(def rtA (runtime/new))
(def rtB (runtime/new))
(runtime/submit rtA '(def only-a 1))
(runtime/drain rtA)
(def probe (runtime/submit rtB 'only-a))
(runtime/drain rtB)
(assert (= (probe :state) :failed) "★ 兩台 runtime 的 env 互相看不到")
(assert (not= (rtA :env) (rtB :env)) "確實是兩張不同的表")

(print "runtime ✓")
