# Notes

本原型把 `outcome_unknown` 寫成 recovery 產生的完整 receipt（stdout/stderr 為 `null`），使 terminal projection 有單一輸入。這是原型的便利選擇，不是 AOS receipt schema 的提案。

另一個待定點是 argv[0]：POSIX `execve` 允許 argv[0] 與 executable 不同，而 CLI 的 `run -- PROGRAM ARG...` 為易用性暫採相同值。正式抽象需決定是否公開這個自由度。
