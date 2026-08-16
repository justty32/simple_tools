# 佇列。重點是那件 ev/chan-close 會咬人的事。

(import ../aos/queue :as queue)

(def q (queue/new 8))
(assert (= (queue/depth q) 0) "新的是空的")
(assert (not (queue/closed? q)) "新的沒關")

(assert (queue/push q :a) "push 成功回 true")
(queue/push q :b)
(assert (= (queue/depth q) 2) "depth 數得對")

(assert (= (queue/take q) :a) "FIFO：先進先出")
(assert (= (queue/depth q) 1) "take 之後 depth 減一")
(assert (= (queue/take q) :b) "第二個")
(assert (= (queue/depth q) 0) "取乾淨了")

# poll 不擋
(assert (nil? (queue/poll q)) "空的 poll 立刻回 nil，不擋")
(queue/push q :c)
(assert (= (queue/poll q) :c) "有東西就取得到")

# ★★ 這是整個模組存在的理由：close 之後，排在前面的還要做得完。
# 直接用 ev/chan-close 的話下面這兩個 assert 都會失敗（緩衝內容全部拿不到）。
(def q2 (queue/new 8))
(queue/push q2 :x)
(queue/push q2 :y)
(assert (queue/close q2) "close 回 true")
(assert (queue/closed? q2) "關了")
(assert (= (queue/depth q2) 2) "★ close 不會丟掉已經排著的")
(assert (= (queue/take q2) :x) "★ close 之後前面排的照取得到")
(assert (= (queue/take q2) :y) "★ 第二個也是")
(assert (nil? (queue/take q2)) "取到哨兵 => nil = 收工")

# close 之後不再收新的
(assert (not (queue/push q2 :z)) "關了之後 push 回 false")
(assert (not (queue/close q2)) "重複 close 回 false，不會再放一個哨兵")

# 哨兵不會被當成資料算進 depth
(def q3 (queue/new 8))
(queue/close q3)
(assert (= (queue/depth q3) 0) "★ 只有哨兵時 depth 是 0，不是 1")
(assert (nil? (queue/take q3)) "空佇列 close 之後立刻收工")

# 資料是任意值，包括 nil 以外的東西；table 也不會跟哨兵搞混
(def q4 (queue/new 8))
(def user-table @{})
(queue/push q4 user-table)
(assert (= (queue/take q4) user-table) "★ 使用者自己的空 table 不會被誤認成哨兵")

(print "queue ✓")
