# env.janet — 造那張「給任務用的獨立 env」，並把 aos/* 內建注入進去。
#
# ── env 是什麼 ───────────────────────────────────────────────────────
#
# Janet 的 env 就是一張 table：symbol key 是綁定（`@{:value 7}`），
# keyword key 是動態變數。`make-env` 造一張以 root-env（core 那 700 多個綁定）
# 為 prototype 的新表，所以：
#
#   * 任務看得到 Janet 標準函式庫的全部（含 `os/`、`file/`、`net/`）；
#   * 任務 `(def x 7)` 落在**這張表**，不會污染 host；
#   * host 自己的 env 任務也看不到。
#
# ⚠ 這是**命名空間隔離，不是安全沙箱**。任務能 `(os/execute …)`、能讀檔、能開 socket。
#   對齊 aos-core 的立場：那條 Unix socket 權限 0600，能送東西進來的人本來就等於
#   有你的 shell，再在語言層擋一層是自欺欺人。真要沙箱就換掉 `make` 的 parent，
#   自己餵一張白名單表——這個檔留了 parent 參數就是為了那天。
#
# ── ★ 為什麼 aos/* 是「注入」不是「import」 ─────────────────────────
#
# 這些函式全部帶著某一個 runtime 的狀態（要往它的 queue 排、要問它的任務表）。
# 寫成一般模組的話它們就得去抓一個全域 runtime，於是「只有一張 env、只有一個
# runtime」會被寫死進架構。改成由 runtime 造好一張 host table 再交進來，
# 這一層就完全不認識 runtime，開兩個 runtime 各配一張 env 也不用改任何程式。
#
# 這也正好是 aos-core 給外掛 `const aos_host *` 的同一招。

(defn bind
  "把一個值綁進 env。doc 會被 `(doc …)` 和 `aos/help` 讀到。"
  [env sym value &opt doc]
  (put env sym @{:value value :doc doc})
  env)

(defn names
  ``這張表**自己**有哪些名字（不含從 core 繼承的七百多個）。

  `all-bindings` 第二個參數 local=true 就是這個意思，漏了它會倒出一整個 core。``
  [env]
  (sort (all-bindings env true)))

(def builtin-names
  "注入的 aos/* 有哪些，照這個順序顯示。"
  ['aos/submit 'aos/self 'aos/task 'aos/tasks 'aos/resume
   'aos/status 'aos/env 'aos/help 'aos/stop])

(defn- render-help [env]
  (def out @"aos/* 內建\n")
  (each sym builtin-names
    (def b (get env sym))
    (when b
      (buffer/push out (string/format "  %-12s %s\n" (string sym) (or (b :doc) "")))))
  (buffer/push out "\n這張 env 自己有的名字：\n  ")
  (buffer/push out (string/join (map string (names env)) " "))
  (buffer/push out "\n")
  (string out))

(defn install
  ``把 aos/* 注入 env。host 是一張函式表，由 runtime 提供：

    :submit  (fn [form] …)      排一個新任務，回傳 task
    :self    (fn [] …)          正在跑的 task（不在任務裡跑就是 nil）
    :task    (fn [id] …)        依 id 查一個 task
    :tasks   (fn [] …)          全部 task
    :resume  (fn [id &opt v] …) 把 :paused 的任務接回去
    :status  (fn [] …)          runtime 狀態
    :stop    (fn [] …)          收工
    :env     (fn [] …)          這張 env 自己

  少給哪個 key 就少注入哪個，所以測試可以只餵一半。``
  [env host]
  (defn maybe [sym k doc]
    (when-let [f (get host k)]
      (bind env sym f doc)))
  (maybe 'aos/submit :submit "(aos/submit form) 排一個新任務進 queue，回傳 task")
  (maybe 'aos/self   :self   "(aos/self) 正在跑的這個 task")
  (maybe 'aos/task   :task   "(aos/task id) 依 id 查一個 task")
  (maybe 'aos/tasks  :tasks  "(aos/tasks) 全部 task，照 id 排序")
  (maybe 'aos/resume :resume "(aos/resume id &opt v) 把 :paused 的任務接回去跑")
  (maybe 'aos/status :status "(aos/status) uptime、做過幾個、queue 還剩幾個")
  (maybe 'aos/env    :env    "(aos/env) 這張 env 自己，拿去 all-bindings 自省")
  (maybe 'aos/stop   :stop   "(aos/stop) 做完手上排著的就收工")
  (bind env 'aos/help (fn [] (render-help env))
        "(aos/help) 列出 aos/* 與這張 env 自己有的名字")
  env)

(defn make
  ``造一張任務用的 env。

  parent 預設是 `root-env`（= core 全套）。host 給了就順便注入 aos/*。``
  [&opt host parent]
  (def env (if parent (make-env parent) (make-env)))
  (if host (install env host) env))
