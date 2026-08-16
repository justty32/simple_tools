# task.janet — 一個任務就是一張表。
#
# 這一層不認識 queue、不認識 fiber、不認識 env，只管「一個任務現在是什麼狀態」。
# 所以它可以單獨測，也可以在完全不起 runtime 的情況下推理狀態機。
#
# ── 狀態機 ───────────────────────────────────────────────────────────
#
#     :queued ──> :running ──┬──> :done        求值跑完了，值在 :value
#                    ^       ├──> :failed      丟了錯誤，:error ＋ :trace
#                    │       ├──> :paused      yield 了，fiber 留在 :fiber
#                    │       └──> :signalled   丟了 user0-7 信號
#                    └───────────┘ (resume)
#
# ★ 這些函式**就地改**傳進來的 table，不是回傳新的。
#   因為 runtime 會把同一個 task 參考交給呼叫端（`(aos/task 3)`），
#   任務跑完之後那個參考要看得到新狀態，換一張新表的話呼叫端會抱著舊的。

(def states
  "所有合法狀態。:queued 與 :running 是過渡，其餘四個是終局（除非 resume）。"
  [:queued :running :done :failed :paused :signalled])

(defn new
  "造一個排隊中的任務。form 就是要求值的那個 list。"
  [id form]
  @{:id id
    :form form
    :state :queued
    :value nil
    :error nil
    :trace nil
    :signal nil
    :fiber nil})

(defn- transition [t to]
  (put t :state to)
  t)

(defn running
  ":queued 或 :paused -> :running。"
  [t]
  (transition t :running))

(defn done
  "跑完了。"
  [t value]
  (put t :value value)
  (put t :fiber nil)
  (transition t :done))

(defn failed
  "丟了錯誤。err 可以是任何值（不只字串）；trace 是渲染好的堆疊字串。"
  [t err trace]
  (put t :error err)
  (put t :trace trace)
  (put t :fiber nil)
  (transition t :failed))

(defn paused
  ``yield 了。value 是 yield 出來的值，fib 是還活著的 fiber。

  ★ fiber 一定要留著，否則這個任務就再也接不回去了。``
  [t value fib]
  (put t :value value)
  (put t :fiber fib)
  (transition t :paused))

(defn signalled
  "丟了 user0-7 信號。sig 形如 :user3。"
  [t sig value]
  (put t :signal sig)
  (put t :value value)
  (put t :fiber nil)
  (transition t :signalled))

(defn finished?
  "終局狀態？:paused 不算——它還接得回去。"
  [t]
  (or (= (t :state) :done)
      (= (t :state) :failed)
      (= (t :state) :signalled)))

(defn resumable?
  "還接得回去嗎？"
  [t]
  (and (= (t :state) :paused) (not (nil? (t :fiber)))))

(defn headline
  ``只有「哪個任務、什麼狀態、跑的是什麼」，不含值。

  給呼叫端想自己決定怎麼渲染值的時候用。⚠ 值一定要自己補，
  `%q` 會把 UTF-8 轉成跳脫位元組（`"中文"` => `"\xE4\xB8\xAD\xE6\x96\x87"`），
  多行字串也會被壓成一行——人要讀的字串得走 `%s`。``
  [t]
  # ⚠ `(string :done)` 是 "done"，沒有冒號；要跟 brief 對齊得自己補回去。
  (string/format "#%d %-10s %q" (t :id) (string ":" (t :state)) (t :form)))

(defn brief
  "一行摘要，給 help／列表用。值走 `%q`，見 `headline` 的提醒。"
  [t]
  (def head (headline t))
  (case (t :state)
    :done      (string/format "%s => %q" head (t :value))
    :failed    (string/format "%s !! %q" head (t :error))
    :paused    (string/format "%s ~~ %q" head (t :value))
    :signalled (string/format "%s %q %q" head (t :signal) (t :value))
    head))
