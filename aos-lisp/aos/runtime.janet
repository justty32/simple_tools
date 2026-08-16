# runtime.janet — 核心 loop 本體。
#
# ── 整台機器就是這五行 ───────────────────────────────────────────────
#
#     (forever
#       (def t (queue/take (rt :queue)))   # 取一個，空的就擋著等
#       (if (nil? t) (break))              # 取到哨兵 = 收工
#       (execute rt t))                    # 在那張 env 裡把它跑掉
#
# 其他所有東西都是為了讓這五行成立：queue 是怎麼收工的、env 從哪來、
# 求值出事怎麼變成資料而不是炸掉。
#
# ── ★ 一次只跑一個 ───────────────────────────────────────────────────
#
# 這個 loop 是**循序**的：`execute` 回來之前不會去取下一個。所以任務之間沒有
# 交錯，那張共用的 env 不需要鎖，也不可能有 race。
#
# 代價是**一個慢任務會擋住全部**（IO 也一樣：任務裡 `(ev/sleep 10)` 就是整台停 10 秒）。
# 這是刻意的：aos-core 選的是「一條連線一條 thread、全部阻塞」，把併發推到連線層、
# 讓每個命令自己寫成同步的；這一版把併發推到 0，讓那張長存 env 連鎖都不用。
# 想要併發的話改的是 `execute`（換成 `ev/go`），loop 本身不用動——但那時候
# 「一張共用 env」就會需要一個說法了。
#
# ── ★ env 不是單例 ──────────────────────────────────────────────────
#
# 現在一個 runtime 配一張 env，只是因為現在這樣最簡單。所以 env 是 rt 上的一個
# **欄位**、是 `evaluate/in-env` 的一個**參數**，沒有任何地方假設它只有一張。
# 要變成一個任務一張、或一個 session 一張，動的是 `execute` 那一行怎麼挑 env。
#
# ⚠ 本檔的定義順序是**有意義的**：`host` 那張表的閉包引用 submit／stop／…，
#   而 Janet 是編譯期查 symbol，引用還沒定義的名字會直接編不過。
#   所以 `host` 和公開建構子 `new` 一定要排在被它們引用的函式後面。

(import ./task :as task)
(import ./queue :as queue)
(import ./evaluate :as evaluate)
(import ./env :as env)

(defn task-by-id
  [rt id]
  (get (rt :by-id) id))

(defn tasks
  "全部任務，照 id 排序。"
  [rt]
  (sorted-by |($ :id) (values (rt :by-id))))

(defn status
  [rt]
  @{:uptime (- (os/clock :monotonic) (rt :started))
    :handled (rt :handled)
    :queued (queue/depth (rt :queue))
    :tasks (length (rt :by-id))
    :running (rt :running)
    :closed (queue/closed? (rt :queue))})

(defn submit
  ``排一個 form 進去，回傳那個 task（狀態 :queued）。

  已經收工的 runtime 會丟錯，而不是安靜地把任務吞掉——安靜吞掉的話呼叫端會
  一直等一個永遠不會跑的任務。``
  [rt form]
  (def id (rt :next-id))
  (put rt :next-id (inc id))
  (def t (task/new id form))
  (unless (queue/push (rt :queue) t)
    (errorf "runtime 已收工，不再接受任務：%q" form))
  (put (rt :by-id) id t)
  t)

(defn- record
  "把一張結果表寫回 task，並跑 on-result。"
  [rt t out]
  (case (out :status)
    :ok     (task/done t (out :value))
    :error  (task/failed t (out :error) (out :trace))
    :paused (task/paused t (out :value) (out :fiber))
    :signal (task/signalled t (out :signal) (out :value)))
  (put rt :handled (inc (rt :handled)))
  (when-let [f (rt :on-result)] (f rt t))
  t)

(defn execute
  ``把一個任務跑掉。**這是 loop 唯一做的事。**

  不丟錯：`evaluate/in-env` 已經把所有出錯路徑變成資料了，所以一個爛任務
  絕對炸不掉 loop。這是「一個壞外掛不會讓 daemon 起不來」的同一條規矩。``
  [rt t]
  (task/running t)
  (put rt :current t)
  (def out (evaluate/in-env (rt :env) (t :form)))
  (put rt :current nil)
  (record rt t out))

(defn resume-task
  ``把 :paused 的任務接回去跑。id 不存在或不是 :paused 就丟錯。

  ★ 這裡是**直接跑**，不是排回 queue。因為 resume 多半是從另一個任務裡叫的
    （`(aos/resume 3)`），呼叫端想當場拿到結果。所以 :current 要存回去，
    不能寫死成 nil——不然巢狀 resume 之後外層任務會以為自己不在跑。``
  [rt id &opt sent]
  (def t (task-by-id rt id))
  (unless t (errorf "沒有 #%d 這個任務" id))
  (unless (task/resumable? t) (errorf "#%d 是 %s，接不回去" id (t :state)))
  (def fib (t :fiber))
  (task/running t)
  (def prev (rt :current))
  (put rt :current t)
  (def out (evaluate/resume-fiber fib sent))
  (put rt :current prev)
  (record rt t out))

(defn stop
  ``收工：往隊尾放哨兵。**排在前面的照做完**，之後 submit 會丟錯。
  對齊 aos-core 的 `aos daemon stop`。``
  [rt]
  (queue/close (rt :queue)))

(defn step
  ``取一個做一個，**不擋**：queue 空的話回 nil。

  給測試和「把目前排著的做完就好」用。真正的 loop 是 `run`。``
  [rt]
  (when-let [t (queue/poll (rt :queue))]
    (execute rt t)))

(defn drain
  "把此刻排著的全部做完，回傳做過的那些 task。不擋。"
  [rt]
  (def finished @[])
  (while (> (queue/depth (rt :queue)) 0)
    (if-let [t (step rt)]
      (array/push finished t)
      (break)))
  finished)

(defn run
  ``★ 核心 loop。擋著取，一次跑一個，取到哨兵就回來。

  要它回來的方法只有一個：`(stop rt)`（或任務裡 `(aos/stop)`）。
  沒人 stop 的話它會一直擋在 `queue/take` 上等下一個任務——那是想要的行為，
  一台 daemon 本來就該閒著等。``
  [rt]
  (put rt :running true)
  (defer (put rt :running false)
    (forever
      (def t (queue/take (rt :queue)))
      (if (nil? t) (break))
      (execute rt t)))
  rt)

(defn- host
  ``交給 env 的那張函式表，全部是抓住這一個 rt 的閉包。

  env 那一層因此完全不認識 runtime：開兩個 runtime 各配一張 env，
  或是測試時餵一張假的 host，都不用改任何程式。
  這就是 aos-core 給外掛 `const aos_host *` 的同一招。``
  [rt]
  @{:submit (fn [form] (submit rt form))
    :self   (fn [] (rt :current))
    :task   (fn [id] (task-by-id rt id))
    :tasks  (fn [] (tasks rt))
    :resume (fn [id &opt v] (resume-task rt id v))
    :status (fn [] (status rt))
    :stop   (fn [] (stop rt))
    :env    (fn [] (rt :env))})

(defn new
  ``開一台。opts：

    :capacity   queue 緩衝大小（預設 1024）
    :env        自己給一張 env；不給就 `env/make` 一張並注入 aos/*
    :on-result  (fn [rt task]) 每個任務跑完（含失敗）叫一次

  ⚠ 自己給 env 的話 aos/* **不會**被注入——你給的那張表歸你負責。
    要自己的 parent 又要 aos/*，用 `(env/make (host rt) parent)`；
    但 host 是私有的，所以實務上是先 `new`、再 `(env/install (rt :env) …)`。``
  [&opt opts]
  (default opts @{})
  (def rt
    @{:queue (queue/new (get opts :capacity 1024))
      :env nil
      :by-id @{}
      :next-id 1
      :current nil
      :handled 0
      :started (os/clock :monotonic)
      :running false
      :on-result (get opts :on-result)})
  # ★ env 要等 rt 存在才造得出來（host 的閉包抓的就是 rt），所以先 nil 再補。
  (put rt :env (or (get opts :env) (env/make (host rt))))
  rt)
