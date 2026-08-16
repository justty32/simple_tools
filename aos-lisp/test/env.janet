# 獨立 env 與 aos/* 的注入。這一層不認識 runtime，所以 host 餵假的就能測。

(import ../aos/env :as env)
(import ../aos/evaluate :as evaluate)

# 沒 host 也能造，就是一張乾淨的表
(def bare (env/make))
(assert (empty? (env/names bare)) "新的 env 自己什麼都沒有")
(assert (deep= ((evaluate/in-env bare '(map inc [1 2])) :value) @[2 3]) "但 core 全套都在")
(assert (cfunction? ((evaluate/in-env bare '(do os/execute)) :value)) "os/ 也在（這不是安全沙箱）")

# 兩張 env 互不相干
(def a (env/make))
(def b (env/make))
(evaluate/in-env a '(def only-in-a 1))
(assert (truthy? (get a 'only-in-a)) "落在 a")
(assert (nil? (get b 'only-in-a)) "★ b 看不到 a 定義的東西")
(assert (= ((evaluate/in-env b '(no-such 1)) :status) :error) "b 裡面確實沒有")

# 自訂 parent：任務看得到 parent 的東西，但寫不進去
(def parent (make-env))
(put parent 'from-parent @{:value :yes})
(def child (env/make nil parent))
(assert (= ((evaluate/in-env child 'from-parent) :value) :yes) "繼承得到 parent 的綁定")
(evaluate/in-env child '(def only-in-child 1))
(assert (nil? (get parent 'only-in-child)) "★ 子表的定義不會往上污染 parent")

# ── 注入 ────────────────────────────────────────────────────────────
(def log @[])
(def fake-host
  @{:submit (fn [form] (array/push log [:submit form]) :submitted)
    :self   (fn [] :the-task)
    :status (fn [] @{:handled 42})
    :stop   (fn [] (array/push log [:stop]) :stopping)})

(def e (env/make fake-host))

(assert (= ((evaluate/in-env e '(aos/submit '(+ 1 2))) :value) :submitted) "aos/submit 通到 host")
(assert (deep= (log 0) [:submit '(+ 1 2)]) "★ form 原樣傳過去，沒有被求值")
(assert (= ((evaluate/in-env e '(aos/self)) :value) :the-task) "aos/self")
(assert (= ((evaluate/in-env e '(get (aos/status) :handled)) :value) 42) "aos/status")
(assert (= ((evaluate/in-env e '(aos/stop)) :value) :stopping) "aos/stop")

# host 沒給的 key 就不注入——測試可以只餵一半
(assert (nil? (get e 'aos/tasks)) "★ host 沒給 :tasks 就不會有 aos/tasks")
(assert (= ((evaluate/in-env e '(aos/tasks)) :status) :error) "叫沒注入的會是錯誤，不是安靜回 nil")

# aos/help 永遠有，而且只列出真的注入了的
(def help ((evaluate/in-env e '(aos/help)) :value))
(assert (string? help) "aos/help 回字串")
(assert (string/find "aos/submit" help) "列出有的")
(assert (not (string/find "aos/tasks" help)) "★ 沒注入的不會出現在 help 裡")

# aos/env：任務拿得到自己的 env 做自省
(def e2 (env/make @{:env (fn [] :placeholder)}))
(assert (= ((evaluate/in-env e2 '(aos/env)) :value) :placeholder) "aos/env 通到 host")

# names 只看本層，不會倒出 core 那七百多個
(evaluate/in-env e '(def mine 1))
(def ns (env/names e))
(assert (find |(= $ 'mine) ns) "本層定義的看得到")
(assert (not (find |(= $ 'map) ns)) "★ 繼承來的 core 綁定不算在本層")
(assert (< (length ns) 50) "本層名字很少，沒把 core 一起倒出來")

# bind 帶的 doc 進得了 env
(def e3 (make-env))
(env/bind e3 'thing 5 "說明文字")
(assert (= (get-in e3 ['thing :value]) 5) "bind 的值")
(assert (= (get-in e3 ['thing :doc]) "說明文字") "bind 的 doc")

(print "env ✓")
