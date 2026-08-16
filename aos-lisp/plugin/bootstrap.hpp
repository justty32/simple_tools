#pragma once
// bootstrap.hpp — 每一條 loop 的 Janet VM 起來時跑一次的那段 Janet。
//
// ★ 這裡是**求值語意**的所在地，也是 C／Janet 分界的 Janet 那一側。
//   C 管：入口、分派、queue、thread、process。
//   Janet 管：一個 form 在一張 env 裡怎麼跑、出事怎麼變成資料。
//
// 這段程式的**回傳值是一個函式**，C 那邊把它 gcroot 起來，之後每個 job 呼叫一次：
//
//     (run-job src out err) -> exit status
//
// 用回傳閉包而不是在 env 裡放一個名字，是刻意的：task-env 被閉包捕獲，
// C 從頭到尾拿不到它，任務也沒有辦法用 `(def run-job …)` 把派工函式蓋掉。

namespace aoslisp {

inline constexpr const char *kBootstrap = R"JANET(
# ★ parent 一定要明寫成 (curenv)，不能靠 make-env 的預設。
#   預設的 parent 是 `root-env`，那不見得是 C 這邊 janet_core_env() 拿到、
#   而且剛剛把 unix/* 裝進去的那張表——實測就是不同的一張，症狀是任務裡
#   `unknown symbol unix/call`。(curenv) 在這裡就是 janet_dobytes 收到的那張 env。
#
# ⚠ 順帶一提：Janet 的註解是 `#`，`;` 是 **splice 運算子**。在這裡寫 `;` 註解
#   會得到 "splice can only be used in function parameters and data constructors"。
(let [task-env (make-env (curenv))]

  # 值怎麼變成文字。字串／buffer 原樣輸出；其他走 %q。
  # ⚠ %q 會把非 ASCII 轉成 \xE4\xB8\xAD 這種跳脫位元組，所以巢狀結構裡的中文
  #   看起來會是天書。這是刻意的取捨：%q 的輸出 parse 回來位元組完全一致，
  #   而且不會有換行或引號跑出來破壞格式。字串本身是最常見的情況，所以特判掉。
  (defn- render [v]
    (if (or (string? v) (buffer? v)) (string v) (string/format "%q" v)))

  (defn- fail [err message]
    (buffer/push err message "\n")
    1)

  (fn run-job [src out err]
    # print 導向這個 job 的 buffer。env table 的 keyword key 就是動態變數，
    # 而 task fiber 的 env 正是 task-env，所以 (dyn :out) 找得到。
    (put task-env :out out)
    (put task-env :err err)
    (var status 0)
    (var last nil)
    (try
      (do
        (each form (parse-all src)
          # ★★ sigmask 是 :ey01234567，不是 :a。
          #    :a 會連 user9(await)／user8(interrupt) 一起擋掉，而 ev 就是用那兩個
          #    實作阻塞 IO 的——結果是任務裡的 IO 安靜地永遠不完成（:suspended，
          #    不報錯）。這裡故意逐項列，把 8 跟 9 留給 ev。
          (def fib (fiber/new (fn [] (eval form)) :ey01234567 task-env))
          (def v (resume fib))
          (def st (fiber/status fib))
          (set last v)
          (cond
            (= st :dead)
            nil

            (= st :error)
            (do (set status 1)
                (with-dyns [:err err :err-color false]
                  (debug/stacktrace fib v ""))
                (break))

            (= st :pending)
            # 任務 yield 了。★ 擋下 :y 仍然是必要的——不擋的話 yield 會穿過
            #   resume 往上炸掉整條 loop。擋下來之後才有機會變成一則乾淨的錯誤。
            #   這條路徑沒有 resume 支援（fiber 沒有人保管），所以照實說。
            (do (set status (fail err "任務 yield 了，但這條路徑沒有 resume 支援；fiber 已丟棄"))
                (break))

            # :user0 … :user7
            (do (set status (fail err (string/format "任務丟出信號 %q，值 %q" st v)))
                (break))))
        (when (= status 0)
          (buffer/push out (render last) "\n")))
      # parse 壞語法之類：在任何一個 form 跑起來之前就失敗了。
      ([e] (set status (fail err (string e)))))
    status))
)JANET";

}  // namespace aoslisp
