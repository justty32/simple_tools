# 核心模型：先把七個角色切開

這是 [`full/01–04`](../full/01-FUNCTION-AND-FILESYSTEM-MEMORY.md) 的濃縮導讀，不是正式 ABI。對 C++ 工程師而言，可以先把 AOS 想成「有持久 journal、能重開的 job runtime」。

## 最小世界觀

**使用者已定：**Function 像 CPU 指令，filesystem（檔案系統）像記憶體；磁碟是 AOS 掛掉後仍存在的慢速記憶體。Function 可由明確檔案或資料夾承載，也能呼叫其他 Function。Linux process（行程）只是最底層 leaf，不是所有 Function。

```text
Function Definition + Call
          --P1 原型暫選：安全接受--> 固定身分的 Task
          --執行-----------------> Attempt / child Tasks
          --已知-----------------> Function Return + 持久證據
```

上圖中，只有「執行中的 Function 實例叫 Task」是使用者直接定義；Call、Attempt、Receipt 的精確名稱與完整生命週期仍是目前推薦或原型暫選。

## 七個角色

| 角色 | 白話對照 | 不可混成 |
|---|---|---|
| Function Definition | 函式本體或 build target；由明確 path 承載 | 某次執行 |
| Call | 一次純請求：目標、參數、輸入 | Task state、parent relation |
| Task | 一次受 AOS 管理的執行實例；可為 composite | PID |
| Attempt | Task 內一次具體啟動，可能有 PID／worker | Task 身分、retry 許可 |
| Function Return | caller 看見的語意結果 | 磁碟上的完成證明 |
| Receipt／持久證據 | 哪個 Task 根據哪些證據得到哪個 Return | P0 裡恰好叫 `receipt.json` 的檔案 |
| Tool Result | Agent 回給模型、用來配對 Tool Call 的結果 | Return 或 Receipt 的別名 |

**目前推薦：**Call 保持純資料；同一 Call 接受兩次可以形成兩個 Task。Task 不是 PID，因為 composite Task 可有多個 children，一個 Task 也可能跨 pause 或重開。P1 暫把「安全接受後固定 Task ID，行程啟動是內部 Attempt」當保存模型，但這不是使用者已固定的正式生命週期。

## Function 組合不是目錄掃描

單檔長成資料夾，若仍只完成一件 leaf 工作，AOS 看到的仍是一個 Function。只有 child 需要獨立保存、恢復、排程或外部作用邊界時，才形成 AOS 可見的 child Task。

```text
parent Call
  -> child A Call -> child A Return
  -> parent policy
  -> child B Call -> child B Return
  -> parent Return
```

Parent 必須明確保存 relation；不能掃 `tasks/` 或 OS process tree 猜父子關係。Task-tree root、Agent root 與 Linux process tree 是三種不同的 root。

## Filesystem memory 也不是一種東西

| 類別 | 主要問題 |
|---|---|
| Definition | 本次跑的是哪版工作定義？ |
| 可變 memory | messages、設定、成果以哪個 base 發布？ |
| 持久證據 | 哪些接受、意圖與結果已不可修改？ |
| 本機執行狀態 | 哪個 worker、PID、claim 仍在本機有效？ |
| 外部世界 | HTTP、寄信或 root 外修改是否可能已發生？ |

因此 immutable Call 不代表它引用的所有檔案都已 snapshot；live、checked-live 與 snapshot 仍要按資料類型選擇。

## 四個最重要的反等號

```text
Function != process
Task     != PID
Return   != Receipt
unknown  != 已知失敗，也 != 自動重試
```

完整原文：[`full/01`](../full/01-FUNCTION-AND-FILESYSTEM-MEMORY.md)、[`02`](../full/02-LEAF-PROCESS-CALL.md)、[`03`](../full/03-CALL-TASK-RETURN.md)、[`04`](../full/04-COMPOSITE-FUNCTION-AND-TASK-TREE.md)。來源狀態總表見 [`full/00`](../full/00-STATUS-AND-SOURCES.md)。
