# 任務狀態機。不起 runtime、不碰 fiber。

(import ../aos/task :as task)

(def t (task/new 1 '(+ 1 2)))
(assert (= (t :state) :queued) "新任務是 :queued")
(assert (= (t :id) 1) "id")
(assert (deep= (t :form) '(+ 1 2)) "form 原樣留著")
(assert (not (task/finished? t)) ":queued 不是終局")

(task/running t)
(assert (= (t :state) :running) ":running")

(task/done t 3)
(assert (= (t :state) :done) ":done")
(assert (= (t :value) 3) "值")
(assert (task/finished? t) ":done 是終局")
(assert (not (task/resumable? t)) ":done 接不回去")

# 失敗保留錯誤與堆疊
(def f (task/new 2 '(error :boom)))
(task/running f)
(task/failed f :boom "trace...")
(assert (= (f :state) :failed) ":failed")
(assert (= (f :error) :boom) "錯誤值可以是 keyword，不只字串")
(assert (= (f :trace) "trace...") "堆疊留著")
(assert (task/finished? f) ":failed 是終局")

# ★ :paused 不是終局，而且一定要留著 fiber
(def p (task/new 3 '(yield 1)))
(def fib (fiber/new (fn [] 1)))
(task/running p)
(task/paused p 1 fib)
(assert (= (p :state) :paused) ":paused")
(assert (not (task/finished? p)) "★ :paused 不算終局——它還接得回去")
(assert (task/resumable? p) ":paused 接得回去")
(assert (= (p :fiber) fib) "★ fiber 留著，否則就再也接不回去了")

# 沒有 fiber 的 :paused 接不回去（防呆）
(def p2 (task/new 4 '(yield 1)))
(task/paused p2 1 nil)
(assert (not (task/resumable? p2)) "沒 fiber 的 :paused 接不回去")

# 信號
(def s (task/new 5 '(signal 3 :hi)))
(task/running s)
(task/signalled s :user3 :hi)
(assert (= (s :state) :signalled) ":signalled")
(assert (= (s :signal) :user3) "信號種類")
(assert (= (s :value) :hi) "信號帶的值")
(assert (task/finished? s) ":signalled 是終局")

# 終局狀態會把 fiber 清掉，不留參考
(task/paused p 1 fib)
(task/done p :later)
(assert (nil? (p :fiber)) "跑完之後 fiber 不留著")

# brief 每個狀態都渲染得出來，不會炸
(each x [t f p2 s (task/new 9 '(:q))]
  (assert (string? (task/brief x)) (string/format "brief 對 %s 有輸出" (x :state))))

(print "task ✓")
