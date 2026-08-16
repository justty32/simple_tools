# evaluate.janet — 在指定的 env 裡求值一個 form。
#
# **整個專案只有這個檔碰 fiber。** 其餘每一層拿到的都是一張講完結果的表，
# 不必知道 fiber 存在。
#
# ── 怎麼換 env ───────────────────────────────────────────────────────
#
# `eval` 永遠在「當前 fiber 的 env」求值，沒有參數可以換。所以要換一張表就
# 開一個 fiber、把 env 交給它。`fiber/new` 的第三個參數就是 env：
#
#     (fiber/new (fn [] (eval form)) sigmask env)
#
# （`fiber/setenv` 也可以，效果一樣，只是多一行。）
#
# ── ★★ sigmask 為什麼是 :ey01234567，不是 :a ────────────────────────
#
# 直覺會想「把全部信號都擋下來最安全」，於是寫 `:a`。這會安靜地弄壞所有 IO：
#
#     mask :a           (ev/sleep 0.05)  =>  (:suspended nil)   ← 永遠不會完成
#     mask :ey01234567  (ev/sleep 0.05)  =>  (:dead :slept)     ← 0.050 秒
#
# 因為 ev 模組**就是用信號實作阻塞 IO 的**：user9 是 await、user8 是 interrupt。
# `:a` 把這兩個一起攔下來，於是「等 IO」這件事被我們攔截後就沒人接手了，
# 任務停在 `:suspended` 不是 `:dead`，而且不會報錯——狀態表上看起來像它還在跑。
#
# 所以要一個一個列：
#
#     :e          錯誤            —— 一定要擋，否則一個壞任務炸掉整個 loop
#     :y          yield           —— 一定要擋，否則 yield 會穿過 resume 往上炸
#     :0 … :7     user0-7         —— 使用者自己 (signal n v) 丟的，當資料收下
#     （不含 :8 中斷、:9 await）    —— ★ 留給 ev，這是 IO 能動的原因
#
# ── 回傳 ─────────────────────────────────────────────────────────────
#
#     @{:status :ok      :value v}
#     @{:status :error   :error e :trace s}
#     @{:status :paused  :value v :fiber f}
#     @{:status :signal  :signal :user3 :value v}

(def sigmask
  ``擋錯誤、yield、user0-7；★ 故意不擋 user8(中斷)／user9(await)。

  改這個常數之前先讀本檔開頭那段——`:a` 會讓任務裡的 IO 安靜地永遠不完成。``
  :ey01234567)

(defn- trace-of
  ``把 fiber 的堆疊渲染成字串。

  `debug/stacktrace` 是寫到 `:err` 這個動態變數指的地方，而它可以是 buffer，
  所以把 buffer 暫時塞進去就攔得到。:err-color false 是為了不要混進 ANSI 碼。``
  [fib err]
  (def out @"")
  (with-dyns [:err out :err-color false]
    (debug/stacktrace fib err ""))
  (string out))

(defn- outcome
  "把 fiber 跑完之後的 (status, value) 翻成一張講完結果的表。"
  [fib value]
  (def st (fiber/status fib))
  (cond
    (= st :dead)    @{:status :ok :value value}
    (= st :error)   @{:status :error :error value :trace (trace-of fib value)}
    (= st :pending) @{:status :paused :value value :fiber fib}
    # :user0 … :user7。:user8/:user9 到不了這裡（我們沒擋），:suspended 也是
    # （那代表 mask 設錯了，見開頭）——真的遇到就照實回報，不要假裝是別的。
    @{:status :signal :signal st :value value}))

(defn in-env
  ``在 env 裡求值 form，回傳一張結果表。不丟錯：壞 form、壞語法、runtime 錯誤
  全部變成 `:status :error`。

  form 是**已經 parse 好的資料**（一個 list／tuple），不是字串。要從字串進來的話
  呼叫端自己先 `parse`——那樣壞語法會在 submit 當下就被抓到，而不是排到隊尾才炸。``
  [env form]
  (def fib (fiber/new (fn [] (eval form)) sigmask env))
  (outcome fib (resume fib)))

(defn resume-fiber
  ``把一個 `:paused` 的 fiber 接回去跑。sent 是要送回給 yield 的值。

  ⚠ fiber 的 env 在 `fiber/new` 當下就決定了，這裡換不掉，也不該換——
  同一個任務中途改 env 的話它前半段和後半段看到的世界會不一樣。``
  [fib &opt sent]
  (outcome fib (resume fib sent)))
