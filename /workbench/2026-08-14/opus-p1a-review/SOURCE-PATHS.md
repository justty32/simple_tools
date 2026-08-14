# 建議閱讀路徑

由少到多；只在需要 code audit 時才讀原始碼與測試。

1. [`round-2 01-DECISIONS`](../round-2-decision/01-DECISIONS.md)：目前已決邊界與 P1a 定位。
2. [`round-2 06-FUNCTION-TASK-MODEL`](../round-2-decision/06-FUNCTION-TASK-MODEL.md)：Function Definition／Call／Task、child protocol、fault matrix。
3. [`P1a prototype README`](../p1a-task-tree-python/README.md) 與 [`NOTES`](../p1a-task-tree-python/NOTES.md)：原型實際做了什麼、心得與缺口。
4. 只有需要審 code／測試時才讀 [`aos_p1a.py`](../p1a-task-tree-python/aos_p1a.py) 與 [`test_p1a.py`](../p1a-task-tree-python/test_p1a.py)。
5. [`P0 Python README`](../p0-function-python/README.md) 與 [`NOTES`](../p0-function-python/NOTES.md) 只作 leaf process 背景；不是 P1a tree 的權威。

根層現有五份 AOS 文件（`AOS-ARCHITECTURE.md`、`AOS-SCHEDULING.md`、`AOS-V0.md`、`AOS-INTEGRATION.md`、`AGENTLOOP-ON-AOS.md`）是舊模型／歷史材料，**不可當作本評審的權威**。

請只在本目錄新增 `OPUS-REVIEW.md`，不要改動本包或上述來源。
