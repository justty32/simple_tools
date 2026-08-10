# 建議架構、ABI 與 plugin 邊界

## 分層

```text
┌──────────────────────────────────────────────────┐
│ Python UX / prototypes / research / provider SDK │
│ callable reflection / _type:python / LiteLLM     │
└──────────────────────┬───────────────────────────┘
                       │ versioned JSON/events or C ABI
┌──────────────────────▼───────────────────────────┐
│ C++ semantic core                                │
│ AgentPath · ToolSpec · Policy · BudgetLedger     │
│ RoundState · TaskState · MemoryTarget · View      │
└──────────────────────┬───────────────────────────┘
                       │ typed intents/results
┌──────────────────────▼───────────────────────────┐
│ Trusted adapters / supervisor                    │
│ HTTP/SSE · fs/SQLite · process · runtime · FUSE  │
└──────────────────────┬───────────────────────────┘
                       │ explicit OS APIs
┌──────────────────────▼───────────────────────────┐
│ Linux namespace · cgroup · seccomp · mount       │
└──────────────────────────────────────────────────┘
```

同一個 CMake project 可以同時產 shared/static library 與 executable；但高權限 supervisor、Python sidecar、provider proxy 不必硬塞同一 process。權限與 crash isolation 比「一個 binary」重要。

## C++ core 擁有的 invariant

- canonical logical path 與 segment-safe ancestry。
- schema/version/unknown-field validation。
- permission subset、mount downgrade、resource conservation。
- Round／Step／tool-call debt／stop reason transition。
- task/report lifecycle 與 operation idempotency。
- memory address、link resolution budget、context manifest。
- actor/instance/grant-generation 綁定的 projection decision。

core 的 public operation 應接 immutable value，回 transition；不得在 getter 裡偷做 HTTP、filesystem 或 global registry mutation。

## adapter 擁有的 effect

- HTTP/TLS/SSE、provider credential、retry transport。
- atomic file、fsync、SQLite transaction、object store。
- spawn、pipe、signal、deadline、stdout/stderr drain。
- container、mount、namespace、cgroup、seccomp。
- FUSE/9P connection 與 kernel callback。

adapter 不自行決定授權；它驗證 intent 完整且由受信 core/supervisor binding 產生，再執行並回 result event。

## 建議 reducer 形狀

```cpp
struct Transition {
    RoundState state;
    std::vector<Intent> intents;
};

Result<Transition> reduce(const RoundState&, const Event&);
```

`Event`／`Intent` 用 closed variant 表達 core 已知操作；tool body type 的開放集合留在 registry/adapter layer。這樣 fuzz core 不需載入 plugin，新增 effect 也不會讓模型直接取得 raw function pointer。

## C ABI

若需要讓 Python、Janet 或其他 host in-process 使用，公開 C ABI，不公開 STL type：

```c
typedef struct freepy_core freepy_core;

int freepy_core_new(const uint8_t *config, size_t size, freepy_core **out);
int freepy_core_step(freepy_core *, const uint8_t *event, size_t size,
                     uint8_t **result, size_t *result_size);
void freepy_bytes_free(uint8_t *);
void freepy_core_free(freepy_core *);
```

實際命名與 wire 要另立 ADR；原則是：caller/callee allocator 成對、buffer ownership 明確、錯誤可取得、exception 不越界、ABI 有版本。若 step 頻率不高，stdio JSON service 更容易部署與復原，未必需要 ABI。

## Plugin 策略

`tooljson` 的 `_type` 是開放集合，但開放不等於「任意 `.dll/.so` 都進主 process」。建議三級：

1. built-in C++ handler：随 application 編譯／startup 註冊。
2. portable exec／stdio handler：預設第三方擴充，process isolation、wire version 清楚。
3. C ABI plugin：只有低延遲或既有 native library 的明確需求才開，需 ABI/version/allocator/thread/unload policy。

`_type:"python"` 永久由 Python sidecar 擁有。C++ registry 收到這類 spec 可回 typed `python.invoke` intent，而不是載入 CPython 與任意 module。只有量測證明 sidecar 開銷不可接受時，才評估 embedding CPython；那會把 GIL與 interpreter lifecycle 納入 TCB。

## LLM transport

近期：

```text
C++ core → normalized request intent → Python/OpenAI adapter → LiteLLM localhost
```

中期可以換 native OpenAI-compatible client，但保持相同 event：message delta、tool-call delta、usage、finish、error。LiteLLM 繼續吸收 DeepSeek／Ollama／LM Studio 差異；不要讓每個 provider 的非標準參數滲進 core type。

streaming parser 需處理任意 chunk boundary、空 choices、usage-only final chunk、reasoning/tool-call fragments、disconnect 與 cancellation。這些要從現有 `Reply` 行為抽 wire fixtures，不靠真模型 CI。

## Threading model

建議每個 Round state 有單一 owner event queue；其他 thread 只 enqueue control event。這比讓 UI、timer、HTTP callback、tool worker 同時鎖一個巨大 `Handle` 更容易證明。

completion 與 `add_instruction` 必須在同一序列化點決定：先被接受就留在本 Round，completion 先 commit 就回 `round already completed`。tool input request/response 以 request id correlation；stop 產 cancellation event，不能只設 atomic flag 留下 blocked worker。

## 可信計算基底

TCB 至少包含：C++ core、C ABI/serialization、supervisor/adapters、dependency、runtime config 與 kernel。若 Python adapter能碰 host root或 secret，它也在 TCB。報告建議把高權限 effect 拆小、獨立 process、以 typed intent 限制；不是宣稱 native binary 本身就是 sandbox。
