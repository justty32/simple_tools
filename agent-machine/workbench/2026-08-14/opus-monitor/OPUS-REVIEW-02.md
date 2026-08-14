# OPUS-REVIEW-02：P1a-2 規格審查

## 結論

四題依序：**(1) 自洽，有一個例外**；**(2) store 內成立，破口在 store 外**；**(3) 合理，缺一條分支**；**(4) 有 2 個 blocker**，兩個都不需要擴大範圍就能修完。

上一輪的 B1–B4 已確實吸收（`roots.jsonl` 兩段式、pre-plan 整份 re-stage、`call_ref` 是 Call 身分而 `call.json` 只是位置、recovery 不呼叫 acceptance resolver），不再重提。以下只列新問題。

## Blocking

### B5. `store.lock` 與被 spawn 的 P0 process 的關係未定義

這是本輪唯一會讓既有測試結論失真的問題，而且它正好落在 `07 §6` 已經要求的「after-spawn kill-parent」上。

`06 §1` 要求 accept／progress／recover 共用 store exclusive lock，`06 §6` 又把「確認舊 worker 停止」當成 recovery 的前置條件——但沒有說這個確認的機制是什麼。若機制是 `flock`，鎖屬於 open file description，**會被 fork/exec 繼承**，於是只有兩種實作，且兩種都錯在不同地方：

- fd 沒有 CLOEXEC：writer 被 SIGKILL 後，orphan 的 P0 仍持有同一個 lock，recovery 會卡住或把「鎖還在」誤讀成舊 worker 還活著。
- fd 有 CLOEXEC：lock 隨 writer 死亡釋放，recovery 於是**與仍在寫 `attempts/attempt-1/p0/<uuid>/` 的 orphan 並行**。binder 這時讀到合法 prefix，判 IncompleteUnknown 並 commit `repair_required`；orphan 隨後補完 stdout／stderr／receipt，七件 raw 就在一個已判 repair 的 leaf 底下變完整。

第二種情況的後果不只是 flaky test：它讓「single writer」這個前提在 P1a-2 第一次不成立，而 P1a-2 的全部聲稱都建立在它上面。`07 §4` 的三態也沒有涵蓋「commit repair 之後 evidence 才變完整」。

最小修正（都在本片範圍內）：

1. 明寫所有 store fd（含 `store.lock`）在 spawn P0 前必須 CLOEXEC，lock 不得被子代繼承。
2. 把「無任何存活的 P0 子代持有 `attempts/`」寫成 recovery 的前置條件，並指明由 harness 保證——after-spawn fixture 必須先確保 P0 子代已結束再重啟，不能只 SIGKILL writer parent。
3. 明寫「已 commit `repair_required(incomplete_evidence)` 後 evidence 變完整」是 corruption，不是遲到的成功。

### B6. `repair_required(generation)` 的 parent 側行為沒有定義

`07 §5` 列了三種 first 結果（符合 oracle／known 但不符 oracle／IncompleteUnknown），`06 §5` 也只提 IncompleteUnknown。但 `08 §2` 的 role grammar 明確允許第四種終局：`accepted -> repair_required(generation)`，而且 `07 §6` 還要求測「generation 在 pre-plan、planned 後 intent 前、raw 完整後改變」三種結果。也就是說：這條路徑必測，但 parent 遇到它該做什麼沒人寫。

不能靠「比照 IncompleteUnknown」帶過，因為兩者的認識論不同：generation 不符時 intent 與 UUID 都是 0，**可以證明沒有發生任何外部作用**；IncompleteUnknown 則是不可知。把兩者混成同一個停止狀態，等於自願丟掉一個你已經握有的區別。

最小修正：在 `07 §5` 補上 generation 分支（建議 parent 不 observe、不建 root Receipt、不 plan second、反覆 recovery 零寫），並給 root 的三種無 Receipt 停滯**各自的 projection 名稱**（child 不可知／child generation／不符 oracle）。目前只有 `waiting_for_parent_policy` 有名字，另兩種沒有；`07 §6` 要求「不能只看 report」，但 report 仍是 crash matrix 的 oracle，停滯狀態不可辨識就沒有可比對的期望值。

## 高把握度（非 blocking）

### H1. ROOT_CALL 沒有任何 Definition 參照

`README` 的 P1 工作模型是 `Function Definition + immutable Call --accepted--> Task`，但 `08 §3` 的 ROOT_CALL 只有 `{"v":1,"kind":"sequence_two","children":[...],"first_success_oracle":...}`——沒有 definition path，也沒有 generation。PROCESS_CALL 有 `definition{absolute_path,generation}`，fake 有 `kind`。結果是：整棵樹裡唯一沒有實例化那條主軸的 Task，正好是 root。

修正很便宜：給 ROOT_CALL 一個顯式的 fixture Definition 欄位（例如 `{"kind":"fixture","name":"sequence_two"}`），讓「每個 Call 都指名自己的 Definition」變成沒有例外的不變量。這不引入 resolver，也不擴大範圍。

同時建議在 `08 §3` 就地標註：ROOT_CALL 內的 `children[].recipe` 與 `first_success_oracle` 其實是 composite **Definition 的行為與 policy**，只因 fixture 沒有真正的 composite body 才寄放在 Call 裡。這句話必須寫死，否則 P1a-1 v2 唯一實證過的不變量——「Call 不含 relation／plan／state」——會在被引用時悄悄變成「Call 可以帶計畫與策略」。

### H2. `derive_child_id` 不是單射

`08 §1`：root ID 是 `[a-z][a-z0-9_-]{0,31}`，`derive_child_id(root,slot) = root + "--" + slot`，而 root 與 child 共用 `tasks/<task-id>/` 命名空間。由於 root ID 允許 `-`，一個叫 `a--first` 的 root 會與 root `a` 的 first child 指向同一個目錄。

fixture 只容許一個 root，所以 P1a-2 內不可達；但這條寫在 `§1` 的是通用格式規則，而且現在改只要一行：禁止 root ID 含 `--`（或改用不在 ID 字元集內的分隔符）。等到多 root 時再改，就要動已落盤的 ID。

## 仍需保留的風險

- `dispatch_intent` 後 0 UUID，依 `08 §5` 的 P0 publish 次序其實可證明「從未 spawn、無外部作用」，但規格把它收進 IncompleteUnknown。作為 fail-closed 沒問題，只是日後不可反過來引用它，聲稱這裡的不可知是不可判定的。
- composite grammar 不含 `repair_required`，所有停滯都只是 projection、沒有持久記錄。「反覆 recovery 零寫」是很強的可測性質，建議保留；代價就是 B6 要求的可辨識命名。
- generation 只證明 intent 前檢查過 live bytes；intent 到七件 raw 完整之間 Definition 仍可能改變而不被偵測。`07 §2` 已誠實寫明，請維持這個措辭強度，不要升級。
- `evidence_hash` 只涵蓋七件 raw；optional `receipt-ready`／`terminal` 同目錄但不進 manifest。目前由一致性檢查兜住，日後若把 `evidence_hash` 說成「UUID 目錄的完整摘要」就會錯。

## 裁決

修完 **B5** 與 **B6** 即可著手 P1a-2；兩者都是規格與 harness 的修正，不動範圍、不需新原型。**H1**、**H2** 是文件與格式的一行修正，建議一併做掉，因為兩者的成本只會隨落盤資料增加。其餘 `06`／`07`／`08` 的 authority 切分、publish barrier 次序與三態 binder，我沒有再找到可以推翻的地方。
