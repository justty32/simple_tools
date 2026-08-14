# P0 Function policy/auditor prototype. Pure data: no process, filesystem, or network IO.

(defn exact-keys? [x allowed]
  (and (dictionary? x)
       (= (length (keys x)) (length allowed))
       (all |(has-key? x $) allowed)
       (all |(not (nil? (index-of $ allowed))) (keys x))))

(defn safe-string? [x max-bytes allow-empty?]
  (and (string? x)
       (<= (length x) max-bytes)
       (or allow-empty? (> (length x) 0))
       (nil? (string/find "\0" x))))

(defn bounded-int? [x low high]
  (and (int? x) (<= low x) (<= x high)))

(defn reject [type code]
  {:type type :decision :reject :code code})

(def function-keys [:executable :argv :cwd :stdin-size :limits :memory-mode])
(def limit-keys [:wall-ms :stdout-max :stderr-max])

(defn valid-argv? [argv]
  (and (array? argv)
       (<= (length argv) 128)
       (all |(safe-string? $ 8192 true) argv)))

(defn valid-limits? [limits]
  (and (exact-keys? limits limit-keys)
       (bounded-int? (get limits :wall-ms) 1 3600000)
       (bounded-int? (get limits :stdout-max) 0 67108864)
       (bounded-int? (get limits :stderr-max) 0 67108864)))

(defn snapshot-argv [argv]
  "Detach a caller-owned array and expose the copied values as a tuple."
  (tuple ;(map string argv)))

(defn snapshot-limits [limits]
  {:wall-ms (get limits :wall-ms)
   :stdout-max (get limits :stdout-max)
   :stderr-max (get limits :stderr-max)})

(defn admit
  "Return a tagged, detached acceptance/rejection snapshot. This never launches a process,
  changes argv, or supplies defaults."
  [description]
  (cond
    (not (dictionary? description)) (reject :admission :not-a-dictionary)
    (not (all |(has-key? description $) function-keys)) (reject :admission :missing-field)
    (not (exact-keys? description function-keys)) (reject :admission :unknown-field)
    (not (safe-string? (get description :executable) 4096 false)) (reject :admission :bad-executable)
    (not (valid-argv? (get description :argv))) (reject :admission :bad-argv)
    (not (safe-string? (get description :cwd) 4096 false)) (reject :admission :bad-cwd)
    (not (bounded-int? (get description :stdin-size) 0 16777216)) (reject :admission :bad-stdin-size)
    (not (valid-limits? (get description :limits))) (reject :admission :bad-limits)
    (not (or (= :live (get description :memory-mode))
             (= :checked (get description :memory-mode))
             (= :snapshot (get description :memory-mode)))) (reject :admission :bad-memory-mode)
    true
    (let [limits (get description :limits)]
      {:type :admission :decision :accept
       :executable (string (get description :executable))
       :argv (snapshot-argv (get description :argv))
       :cwd (string (get description :cwd))
       :stdin-size (get description :stdin-size)
       :limits (snapshot-limits limits)
       :memory-mode (get description :memory-mode)})))

(def hex-digits "0123456789abcdef")

(defn sha256? [x]
  (and (safe-string? x 64 false)
       (= (length x) 64)
       (all (fn [i]
              (not (nil? (string/find (string/slice x i (+ i 1)) hex-digits))))
            (range 0 64))))

(defn stream-summary? [x]
  (and (exact-keys? x [:size :sha256])
       (bounded-int? (get x :size) 0 67108864)
       (sha256? (get x :sha256))))

(defn valid-termination? [x]
  (and (dictionary? x)
       (case (get x :kind)
         :exited (and (exact-keys? x [:kind :exit-status])
                      (bounded-int? (get x :exit-status) 0 255))
         :signaled (and (exact-keys? x [:kind :signal])
                        (safe-string? (get x :signal) 64 false))
         :launch-error (and (exact-keys? x [:kind :stage :message])
                            (or (= :resolve-executable (get x :stage))
                                (= :create-process (get x :stage)))
                            (safe-string? (get x :message) 4096 false))
         false)))

(defn snapshot-termination [x]
  (case (get x :kind)
    :exited {:kind :exited :exit-status (get x :exit-status)}
    :signaled {:kind :signaled :signal (string (get x :signal))}
    :launch-error {:kind :launch-error
                   :stage (get x :stage)
                   :message (string (get x :message))}))

(defn snapshot-stream [x]
  {:size (get x :size) :sha256 (string (get x :sha256))})

(defn audit-receipt
  "Audit outcome metadata. :outcome-unknown is deliberately not a process receipt."
  [outcome]
  (cond
    (not (dictionary? outcome)) (reject :receipt-audit :not-a-dictionary)
    (= :outcome-unknown (get outcome :outcome))
    (if (and (exact-keys? outcome [:outcome :reason])
             (safe-string? (get outcome :reason) 4096 false))
      {:type :receipt-audit :decision :accept :classification :outcome-unknown
       :reason (string (get outcome :reason))}
      (reject :receipt-audit :bad-outcome-unknown))
    (= :receipt (get outcome :outcome))
    (cond
      (not (exact-keys? outcome [:outcome :termination :stdout :stderr]))
      (reject :receipt-audit :bad-receipt-shape)
      (not (valid-termination? (get outcome :termination)))
      (reject :receipt-audit :bad-termination)
      (not (stream-summary? (get outcome :stdout)))
      (reject :receipt-audit :bad-stdout)
      (not (stream-summary? (get outcome :stderr)))
      (reject :receipt-audit :bad-stderr)
      true
      {:type :receipt-audit :decision :accept :classification :complete-receipt
       :termination (snapshot-termination (get outcome :termination))
       :stdout (snapshot-stream (get outcome :stdout))
       :stderr (snapshot-stream (get outcome :stderr))})
    true (reject :receipt-audit :bad-outcome-kind)))

(defn choose-memory-mode
  "Pure policy only; does not inspect filesystem state."
  [requested concurrent-writer?]
  (cond
    (not (or (= requested :live) (= requested :checked) (= requested :snapshot)))
    {:type :memory-policy :decision :reject :code :bad-memory-mode}
    (= requested :live)
    {:type :memory-policy :decision :accept :mode :live :reason :caller-accepts-live-view}
    concurrent-writer?
    {:type :memory-policy :decision :accept :mode :snapshot :reason :concurrent-writer-needs-stable-bytes}
    true
    {:type :memory-policy :decision :accept :mode :checked :reason :no-writer-declared-check-before-commit}))

# Minimal, dependency-free tests.
(def valid-hash "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
(def valid-description
  {:executable "C:/Program Files/tool.exe"
   :argv @["" "has spaces" "$(still-data)" "semi;colon"]
   :cwd "C:/work"
   :stdin-size 0
   :limits {:wall-ms 1000 :stdout-max 1024 :stderr-max 1024}
   :memory-mode :checked})
(def valid-receipt
  {:outcome :receipt
   :termination {:kind :exited :exit-status 0}
   :stdout {:size 0 :sha256 valid-hash}
   :stderr {:size 12 :sha256 valid-hash}})

(defn assert= [label expected actual]
  (unless (= expected actual)
    (error (string "assertion failed: " label))))

(defn assert-true [label value]
  (unless value (error (string "assertion failed: " label))))

(defn run-tests []
  (def admitted (admit valid-description))
  (assert= "valid description accepted" :accept (get admitted :decision))
  (assert= "argv snapshot preserves caller values" (tuple ;(get valid-description :argv)) (get admitted :argv))
  (assert-true "no guessed default" (not (has-key? admitted :default)))
  (assert= "deterministic admission" admitted (admit valid-description))
  (assert= "empty argv item and shell-like text are data" :accept
            (get (admit valid-description) :decision))
  (assert= "NUL rejected" :bad-argv
            (get (admit (merge valid-description {:argv @["a\0b"]})) :code))
  (assert= "mutable buffer is not a string argv item" :bad-argv
            (get (admit (merge valid-description {:argv @[(buffer "x")]})) :code))
  (assert= "stdin size range" :bad-stdin-size
            (get (admit (merge valid-description {:stdin-size 16777217})) :code))
  (assert= "limits require all fields" :bad-limits
            (get (admit (merge valid-description {:limits {:wall-ms 1 :stdout-max 1}})) :code))
  (assert= "unknown function field rejected" :unknown-field
            (get (admit (merge valid-description {:oops true})) :code))
  (def mutable-argv @["first" "second"])
  (def mutable-limits @{:wall-ms 1000 :stdout-max 10 :stderr-max 20})
  (def mutable-description @{:executable "tool.exe" :argv mutable-argv :cwd "C:/work"
                             :stdin-size 0 :limits mutable-limits :memory-mode :checked})
  (def detached-admission (admit mutable-description))
  (put mutable-argv 0 "changed-after-return")
  (put mutable-limits :wall-ms 1)
  (assert= "returned argv is detached from caller array" "first"
            (get (get detached-admission :argv) 0))
  (assert= "returned limits are detached from caller table" 1000
            (get (get detached-admission :limits) :wall-ms))
  (assert= "valid exited receipt" :complete-receipt
            (get (audit-receipt valid-receipt) :classification))
  (assert= "missing exited status" :bad-termination
            (get (audit-receipt (merge valid-receipt {:termination {:kind :exited}})) :code))
  (assert= "signaled requires signal" :bad-termination
            (get (audit-receipt (merge valid-receipt {:termination {:kind :signaled}})) :code))
  (assert= "launch error requires stage and message" :bad-termination
            (get (audit-receipt (merge valid-receipt {:termination {:kind :launch-error :stage :create-process}})) :code))
  (assert= "invalid receipt kind" :bad-termination
            (get (audit-receipt (merge valid-receipt {:termination {:kind :other}})) :code))
  (assert= "invalid stdout hash" :bad-stdout
            (get (audit-receipt (merge valid-receipt {:stdout {:size 0 :sha256 "not-a-hash"}})) :code))
  (def mutable-termination @{:kind :exited :exit-status 0})
  (def mutable-stdout @{:size 0 :sha256 valid-hash})
  (def mutable-stderr @{:size 12 :sha256 valid-hash})
  (def mutable-receipt @{:outcome :receipt :termination mutable-termination
                          :stdout mutable-stdout :stderr mutable-stderr})
  (def detached-receipt (audit-receipt mutable-receipt))
  (put mutable-termination :exit-status 77)
  (put mutable-stdout :size 99)
  (put mutable-stderr :sha256 "bad")
  (assert= "returned termination is detached" 0
            (get (get detached-receipt :termination) :exit-status))
  (assert= "returned stdout is detached" 0
            (get (get detached-receipt :stdout) :size))
  (assert= "returned stderr is detached" valid-hash
            (get (get detached-receipt :stderr) :sha256))
  (assert= "unknown is separate from a process receipt" :outcome-unknown
            (get (audit-receipt {:outcome :outcome-unknown :reason "agent stopped before observation"}) :classification))
  (assert= "unknown cannot carry receipt fields" :bad-outcome-unknown
            (get (audit-receipt {:outcome :outcome-unknown :reason "x" :termination {:kind :exited :exit-status 0}}) :code))
  (assert= "concurrent memory becomes snapshot" :snapshot
            (get (choose-memory-mode :checked true) :mode))
  (assert= "live remains explicit" :live (get (choose-memory-mode :live true) :mode))
  (print "p0_function_policy: 25 tests passed"))

(when (= (dyn :current-file) (get (dyn :args) 0))
  (run-tests))
