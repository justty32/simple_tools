# 限制：做得到的和做不到的

`agentloop` 擋得住的東西分兩半。**上半做完了，下半只有規劃。**

## 做完了：迴圈自己數得出來的

```python
agentloop.run(bot, dispatch, "…", limits=agentloop.Limits(
    steps=20,                         # 最多走幾步
    calls=40,                         # 工具總共叫幾次
    per_tool={"run_shell": 5},        # 某支工具最多幾次
    tools=["read_file", "run_shell"], # 只准用這些
    engines=["deepseek-chat"],        # 只准用這些引擎
    seconds=300, tokens=200_000,
    quiet=1,                          # 連續幾步不叫工具就當它講完
))
```

不給就是 `Limits()`：12 步，其餘不限。

**擋法分兩種，這是這一包最重要的一個決定：**

- **預算真的沒了** → 停整個 agent（Step、時間、token、總次數、引擎）
- **只是這支工具不能用** → **回一句話給模型**（白名單、單一工具用滿）

第二種不是錯誤，是情報。模型讀到
`Error: run_shell has already been used 5 times, which is its limit`
會換個方法繼續做事；整條停掉的話，它連「我可以用別的辦法」都沒機會想。

### `quiet` 是什麼

預設 `1`：模型一不叫工具就收工（也就是「直到它不再呼叫工具」）。

調大就會**推它一把再給幾次機會**——小模型很常直接講一段「我打算這樣做」
就不動了。代價是它**真的**講完的那次也會被多推幾下，白燒幾步。

## 還沒做：作業系統那一層

**以下都還沒寫程式，是要審的東西。**

上面那些擋不到的另一半 —— **cpu / gpu / 記憶體 / 網路 / 能碰哪些檔案**，
也就是「一個函式最多能用掉多少機器」。

那一半**刻意不放進 `Limits`**：設得下去卻沒人擋的欄位，比沒有那個欄位更糟 ——
一個以為自己被關住的 agent，比一個沒關的危險。

## 為什麼這是完全不同的問題

已經做好的那些，共同點是**迴圈是唯一的關卡** —— 工具要跑，一定經過
`_settle()`，在那裡數一下就擋得住。

機器資源沒有這個性質。`run_shell("python train.py")` 一旦 fork 出去，
那個行程要吃多少記憶體、開幾條連線，`agentloop` 一個字都插不上嘴。
**擋得住的只有作業系統。**

所以形狀不一樣：不是「在迴圈裡多一個 if」，是「把工具放進一個盒子裡跑」。

## 盒子有三種，能力差很多

| 盒子 | 擋得住 | 代價 |
|---|---|---|
| `resource.setrlimit` + `preexec_fn` | 記憶體、CPU 秒數、開幾個檔、行程數 | 幾乎沒有，標準庫就有 |
| cgroup v2 | 上面全部 ＋ **真的 CPU 配額、IO 頻寬** | 要寫 `/sys/fs/cgroup`，通常要 root 或 systemd 授權 |
| 容器 / VM | 上面全部 ＋ **網路、檔案系統、gpu** | 要先有 image，啟動慢，跨機器同步麻煩 |

**這三個不是三選一，是三層。** 先做第一層，因為它現在就做得到而且不用權限。

## 第一層：一個真的做得到的版本

```python
Sandbox(memory="2G", cpu_seconds=60, files=256, procs=32)
```

只服務**外部行程**（`base_tools.run_shell` 和 `tooljson` 的 `_type: "exec"`），
做法是 `subprocess.Popen(..., preexec_fn=...)`，在 fork 之後 exec 之前
把 rlimit 設下去。撞到上限的行程會拿到 `MemoryError` / `SIGXCPU` 死掉，
輸出照樣收得回來，變成一句話送回模型 —— **跟現在「工具壞掉」走同一條路**。

**這一層擋不住 python 函式**（`_type: "python"`、`llms.to_tools()` 那些）。
rlimit 是 per-process 的，那些函式跟迴圈在同一個行程裡，設下去等於限制自己。
要限制它們就得把它們也丟到子行程裡跑 —— 那是另一個決定，先不做。
**這件事要寫在文件上，不能讓人以為 `Sandbox` 一設就全包了。**

## 網路和檔案

這兩個 rlimit 管不了。

- **檔案**：`base_tools` 的 root 已經關住四個工具了，但 `run_shell` 關不住
  （`README.md` 自己講了）。真的要關，答案是 `bwrap` / `unshare --mount`
  或容器 —— 不是黑名單。
- **網路**：只有 netns（`unshare -n`）或容器擋得住。
  「不准連外網但可以連本機 proxy」這個常見需求，需要 netns + 一條 veth，
  複雜度直接跳一級。

所以這兩項的誠實狀態是：**現在沒有，也不打算在這一包裡假裝有。**
`agentloop` 的文件裡就寫「要放生會自己動的 agent，答案是容器或 VM」，
跟 `base_tools` / `tooljson` 講的是同一句話。

## GPU

`CUDA_VISIBLE_DEVICES` 決定看得到哪幾張卡，這個做得到而且很便宜。
**「最多用幾 GB 顯存」做不到** —— 那是 driver 的事，nvidia 沒有給
per-process 的硬上限（MPS 有，但要另外跑一個 daemon）。

所以能寫進規劃的只有「哪幾張卡」，不是「多少顯存」。

## 那 token 呢

已經做了（`Limits.tokens`），但**它只算得到模型自己回報的那些**。
一個工具自己去打別的 LLM（很快就會發生）花掉的 token，這裡看不到。

真的要算全，得讓所有出去的 LLM 呼叫都走同一個地方 —— 那個地方是 proxy，
不是 `agentloop`。**litellm 本來就有 budget 功能**，那條路比在這裡加欄位對。

## 順序

1. **先把「哪些擋得住、哪些擋不住」寫清楚**（就是這份）—— 這件事比實作重要。
2. `Sandbox` 的 rlimit 版本，只接外部行程。
3. `CUDA_VISIBLE_DEVICES`。
4. cgroup v2 —— 等真的撞到「CPU 被一個工具吃滿」再說。
5. 容器 —— 那時候要決定的已經不是這一包的事了。
