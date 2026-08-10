# 來源與推論邊界

以下優先列正式規格、官方文件與原始系統論文。Agent Machine 的對應關係是本 repo 的設計
推論，不表示來源作者主張 LLM agent 應如此實作。

## Plan 9

- [Plan 9 from Bell Labs](https://9p.io/sys/doc/9.html)：三原則、9P transactions、per-process
  namespace、`rfork` 資源分享，以及 `/proc` 只是 process view 的限制。
- [The Use of Name Spaces in Plan 9](https://9p.io/sys/doc/names.html)：mount/bind/union namespace、
  file server/proxy，以及 process creation/shared memory 不宜強塞進 file metaphor。
- [Plan 9 `rfork`](https://9fans.github.io/plan9port/man/man3/rfork.html)：以 flags 選擇 process
  resources 的 copy/share/new。
- [Plan 9 file-server introduction](https://9fans.github.io/plan9port/man/man4/intro.html)：synthetic
  file tree、private namespace 與 file-descriptor bridging。

採用推論：per-agent namespace、provider-first file protocol、handle/fid、inheritance manifest、
`status/events/ctl` projection。未採用：path 等於 authority、用文字檔取代 typed lifecycle。

## Linux

- [cgroup v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)：hierarchical controllers、weight、
  `min/low/high/max`、accounting、events 與 delegation。
- [PSI](https://docs.kernel.org/accounting/psi.html)：以 stalled time 而非單看 usage 表示 pressure。
- [pidfd](https://man7.org/linux/man-pages/man2/pidfd_open.2.html)：可 poll、signal、wait 的穩定
  process handle，避免只靠可重用 PID。
- [seccomp-BPF](https://docs.kernel.org/userspace-api/seccomp_filter.html) 與
  [Landlock](https://docs.kernel.org/userspace-api/landlock.html)：縮小 process 的 syscall、filesystem
  與 network attack surface；兩者都不是 agent semantic permission 的替代品。
- [FUSE](https://docs.kernel.org/filesystems/fuse/index.html)：agentfs 的 userspace mount adapter。
- [`sched_ext`](https://docs.kernel.org/scheduler/sched-ext.html)：能擴充實體 CPU task scheduler；不會
  自動排程遠端 endpoint Step attempt。
- [Linux kernel driver interface](https://docs.kernel.org/process/stable-api-nonsense.html)：Linux
  不承諾穩定的 in-kernel module API；支持先使用穩定 userspace ABI。

採用推論：physical 與 cognitive resource controllers 分層；借 cgroup 的 hierarchy/controller
shape，不假裝 token 是 Linux controller。

## Lisp／Lisp machine

- [Common Lisp environments](https://www.lispworks.com/documentation/HyperSpec/Body/03_aa.htm)：
  environment 是求值所需的 bindings 與資訊，且區分 namespace／lexical／dynamic environment。
- [Common Lisp condition system](https://www.lispworks.com/documentation/HyperSpec/Body/09_a.htm) 與
  [restarts](https://www.lispworks.com/documentation/HyperSpec/Body/09_adb.htm)：偵測 condition 與選擇
  restart 分離，handler 可處理、拒絕或延後。
- [Symbolics world save 操作資料](https://www.bitsavers.org/pdf/symbolics/software/release_6/996105_Installation_and_Site_Operations_Mar85.pdf)：
  歷史 Lisp machine 將可啟動 world image 作為系統管理單位。

採用推論：可檢查 object model、event reduction、task-scoped dynamic bindings、typed condition/
restart、portable logical image。未採用：任意 heap dump 作 durability、未授權 `eval`、用 dynamic
binding 取代 capability enforcement。
