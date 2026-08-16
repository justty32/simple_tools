# init.janet — 門面。`(import aos)` 之後五個子模組都在。
#
#   aos/runtime    核心 loop、submit、run。★ 九成場合只需要這個
#   aos/queue      FIFO ＋ 哨兵收工
#   aos/task       任務的狀態機（純資料）
#   aos/evaluate   唯一碰 fiber 的地方：在指定 env 求值
#   aos/env        造那張獨立 env、注入 aos/*
#
# 最短的用法：
#
#     (import aos)
#     (def rt (aos/runtime/new))
#     (aos/runtime/submit rt '(+ 1 2))
#     (aos/runtime/stop rt)
#     (aos/runtime/run rt)
#     ((aos/runtime/task-by-id rt 1) :value)   # => 3
#
# :export true = 別人 import 這個門面時，這幾個子模組也一起帶出去。

(import ./task :as task :export true)
(import ./queue :as queue :export true)
(import ./evaluate :as evaluate :export true)
(import ./env :as env :export true)
(import ./runtime :as runtime :export true)
# 擴充。★ 只是 import 進來，**不會**自動注入任何 env——
# 要 process 能力得自己 (aos/unix/install (rt :env))。
(import ./unix :as unix :export true)
