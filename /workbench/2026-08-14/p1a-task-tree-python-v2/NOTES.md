# P1a-1 v2 使用筆記

> 實驗心得，不是正式設計結論。

## 使用感受

把 Call、Task、relation 拆開後，讀 store 時明顯比 v1 直覺：看某個 child 的 `call.json` 就知道它被要求做什麼；要知道誰要求它、何時可跑，才回 parent events。這也自然表達「相同 Call 可產生多個 Task」。代價是每個 child 都有少量重複 Call bytes；對 P1a 我認為值得，日後若改 CAS，仍可保留 Task 的 `call_ref` 介面。

最值得保留的限制是：unlinked child 只是 staging/orphan，既不列 tree 也不 dispatch。它避免用掃目錄推導關係，但也表示清理 orphan 與長期 CAS GC 必須另設計。

## 已知限制

- `lstat` 現有 root ancestor、Task/payload 與權威檔案的拒絕只是在此模型避免明顯 symlink；沒有 `openat`/fd-based TOCTOU 防護，也不是安全 sandbox。
- report 是純 Projection 閱讀：不會藉機修 torn tail。只有下一個 recovery/progress 才會在已驗 prefix 後截尾、fsync，避免「看報表改了狀態」的意外。
- fake Result 只有 success 或 unknown；沒有 P0 process、stdout binary、signal、timeout、retry 或共通結果 ABI。
- 一個 writer、固定兩 slots、無 migration。parent root Call acceptance 是預先建立，故不驗根接受時 crash。
- `repair_required` 是 fail-closed 停止點，不表示能自動修好或恢復外部效果。

## 是否進 P1a-2

值得先等 Opus 對 schema、relation ownership 與 replay 不變量裁決。若他接受，P1a-2 的最小工作是把一個 leaf 的 fake receipt staging 換成 P0 已驗的 deterministic staging evidence；完整 staged evidence 只能補 commit，缺失 evidence 必須維持 unknown，不重 dispatch。
