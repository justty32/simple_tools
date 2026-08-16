# queue.janet — 任務佇列。FIFO，單一消費者。
#
# 底層是 `ev/chan`，但**不用 `ev/chan-close` 收工**，改用一個私有哨兵值。
#
# ── ★ 為什麼不用 ev/chan-close ──────────────────────────────────────
#
# `ev/chan-close` 會讓緩衝區裡「已經送進去但還沒被取走」的東西**再也拿不到**：
#
#     (ev/give ch :a) (ev/give ch :b)
#     (ev/count ch)      # => 2   東西還在
#     (ev/chan-close ch)
#     (ev/take ch)       # => nil ★ 但一個都讀不到了
#
# 對這裡來說那等於「叫它收工，結果排在前面的任務被安靜地丟掉」。
# aos-core 的 `aos daemon stop` 語義是**做完手上的事之後**才收工，
# 這一版要一樣，所以 close 的做法是往隊尾放一個哨兵：哨兵前面的照做，
# 消費者取到哨兵才回 nil（= 收工）。
#
# ⚠ 這個模組的 `take` 遮住 core 的 `take`，`new`／`push` 也是自己的。
#   模組內部沒有用到被遮住的那幾個 core 函式；從外面看是 `queue/take` 不會混淆。

# 私有哨兵。用空 table 是因為 table 的 `=` 是身分比較，
# 所以使用者送任何資料都不可能「剛好等於」它。
(def- STOP @{})

(defn new
  "開一個佇列。capacity 是緩衝大小；滿了 push 會擋住送的那條 fiber。"
  [&opt capacity]
  (default capacity 1024)
  @{:chan (ev/chan capacity)
    :closed false
    :pending 0})

(defn closed?
  [q]
  (q :closed))

(defn depth
  ``還排著幾個（不含哨兵）。

  自己數而不是問 `ev/count`，因為 close 之後哨兵也躺在 chan 裡，
  `ev/count` 會多算一個。``
  [q]
  (q :pending))

(defn push
  ``排一個進去。已經 close 的佇列回 false，不丟錯——呼叫端多半想自己決定
  要當錯誤還是安靜忽略。``
  [q item]
  (if (q :closed)
    false
    (do
      (ev/give (q :chan) item)
      (put q :pending (inc (q :pending)))
      true)))

(defn take
  ``取一個出來，空的話**擋住**等。取到哨兵回 nil，代表收工。

  這是核心 loop 唯一的取法。``
  [q]
  (def item (ev/take (q :chan)))
  (if (= item STOP)
    nil
    (do
      (put q :pending (dec (q :pending)))
      item)))

(defn poll
  ``取一個出來，空的話**立刻**回 nil，不擋。

  給「把目前排著的做完就好」用（測試、單次批次）。
  ⚠ 回 nil 分不出「空了」和「收工了」——要分清楚就用 take。``
  [q]
  (if (> (ev/count (q :chan)) 0)
    (take q)
    nil))

(defn close
  ``收工：往隊尾放哨兵。前面排著的照做完，之後 push 一律回 false。

  重複 close 是安全的（第二次直接回 false，不會再放一個哨兵）。``
  [q]
  (if (q :closed)
    false
    (do
      (put q :closed true)
      (ev/give (q :chan) STOP)
      true)))
