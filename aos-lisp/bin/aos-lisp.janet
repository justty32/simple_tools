#!/usr/bin/env janet
# aos-lisp.janet — 把核心 loop 開起來給人看的驅動程式。
#
#     janet bin/aos-lisp.janet                    # 互動
#     echo '(+ 1 2) (def x 7) (* x 6)' | janet bin/aos-lisp.janet
#
# ★ 這一層**不是**核心的一部分。核心是 aos/runtime，它只認得已經 parse 好的
#   form；「字串從哪來、怎麼變成 form」是驅動程式的事。之後接 Unix socket、
#   接檔案、接別的行程，換掉的都只有這個檔。
#
# ── 為什麼用 parser/* 而不是一行一個 parse ──────────────────────────
#
# 因為 form 會跨行：
#
#     (defn f [x]
#       (* x 2))
#
# 一行一個 `parse` 的話第一行就是壞語法。`parser/consume` 是**串流式**的：
# 餵幾行都行，湊滿一個完整的 form 才吐出來，沒湊滿就繼續等下一行。
# 這也是 REPL 能處理多行輸入的機制。

(import ../aos/runtime :as runtime)
(import ../aos/task :as task)

(def interactive? (os/isatty stdin))

(defn- show
  ``每個任務跑完叫一次。

  ★ 字串值走 `%s` 另外印，不走 `brief` 的 `%q`：`%q` 會把中文轉成 `\xE4\xB8\xAD`
    那種跳脫位元組、多行也會被壓成一行，`(aos/help)` 的輸出就會整段變成天書。``
  [_ t]
  (def v (t :value))
  (if (and (= (t :state) :done) (or (string? v) (buffer? v)))
    (do (print (task/headline t) " =>") (print v))
    (print (task/brief t)))
  (when (= (t :state) :failed)
    (prin (t :trace))
    (flush)))

(defn- feed
  ``把一段文字餵進 parser，湊出幾個完整的 form 就 submit 幾個。

  壞語法就地報掉並重開一個 parser——★ 不能讓它排進 queue，
  不然一個打錯的括號會讓後面每一行都跟著錯。``
  [rt p chunk]
  (parser/consume p chunk)
  (var parser p)
  (while (parser/has-more parser)
    (def form (parser/produce parser))
    # ⚠ 同一行裡先 (aos/stop) 再寫別的東西時，stop 要等 drain 才會執行，
    #   所以這裡還收得下；但 queue 真的關了之後 submit 會丟錯，接住它。
    (try
      (runtime/submit rt form)
      ([err] (eprintf "沒收下 %q：%s" form err))))
  (when-let [err (parser/error parser)]
    (eprintf "語法錯誤：%s" err)
    (set parser (parser/new)))
  parser)

(defn main [&]
  (def rt (runtime/new @{:on-result show}))
  (var p (parser/new))
  (when interactive?
    (print "aos-lisp — 一個 loop，一張 env。(aos/help) 看有什麼，(aos/stop) 收工。"))
  (while true
    (when interactive? (prin "aos> ") (flush))
    (def line (file/read stdin :line))
    (if (nil? line)
      (break)                                # stdin 結束
      (do
        (set p (feed rt p line))
        # 排一批、跑一批。★ 用 drain 不是 run：run 會擋著等下一個任務，
        # 而下一個任務要等我們回來讀 stdin 才會有——那就互相等了。
        (runtime/drain rt)
        (when (get (runtime/status rt) :closed) (break))))) # 任務叫了 aos/stop
  # stdin 完了還沒 stop 的話，把剩下的做完再走。
  (runtime/stop rt)
  (runtime/run rt)
  (def s (runtime/status rt))
  (when interactive?
    (printf "\n收工：跑過 %d 個任務，%.2f 秒" (s :handled) (s :uptime))))
