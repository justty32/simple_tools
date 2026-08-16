(declare-project
  :name "aos-lisp"
  :description "AOS 核心 loop 的 Lisp 版：一個迴圈，從 queue 取一個 list，在一張獨立 env 裡跑掉"
  :version "0.1.0")
# 刻意沒有 :dependencies——跟 aos-core 一樣，這一層零相依。
# 要用到 spork 才裝，別為了一個 JSON 就把整包拉進來。

# ⚠ :source 要**逐檔列**，不要只寫目錄。jpm 是 cp -rf 過去的，給目錄會變成
#   <modpath>/aos/aos/…（多包一層），import 路徑就跑掉了。
(declare-source
  :prefix "aos"
  :source ["aos/init.janet"      # 門面
           "aos/task.janet"      # 任務狀態機（純資料）
           "aos/queue.janet"     # FIFO ＋ 哨兵收工
           "aos/evaluate.janet"  # 唯一碰 fiber 的地方
           "aos/env.janet"       # 造獨立 env、注入 aos/*
           "aos/runtime.janet"   # 核心 loop
           "aos/unix.janet"])    # 擴充：一次 Unix 呼叫變成一個 list

(declare-executable
  :name "aos-lisp"
  :entry "bin/aos-lisp.janet"
  :install false)
