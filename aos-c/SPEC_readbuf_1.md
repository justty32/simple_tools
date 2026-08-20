# 規格:C API 新增 aos_instruction_read_buffer(從記憶體讀,不必 FILE*)

狀態:**待實作**。

## 背景:為什麼要加

C API 存在的明講目的是**跨語言綁定**(見 README / capi.md)。但寫入端已有 FILE-free
的 `aos_instruction_write_buffer`,讀取端卻只有 `aos_instruction_read(FILE *, ...)`
—— 別的語言(Python / Rust / Go / C#⋯)造不出乾淨的 `FILE *`,於是被逼著開一個真的
檔案或用 `fmemopen` 這種非可攜招數。這個不對稱就是唯一一個「參數不通用」的破口。

補一個對稱的讀取版,整套 C ABI 就能**只用字串與數值驅動**,一個 `FILE *` 都不碰。

## 定案的核心決定

- **一次讀一筆**,語意與 `aos_instruction_read` 完全一致(同樣的 `InstState`、同樣
  的九行格式、同樣的預算),差別只在來源是記憶體而不是 `FILE *`。
- **回報吃掉幾個 byte**(`*consumed`),讓呼叫端能往前推進讀下一筆:
  `buffer += consumed; size -= consumed;`。這是「用一段 buffer loop 出所有記錄」的
  關鍵,也是 memory 版比 FILE 版多出來的唯一東西。
- **零複製**:用一個唯讀的 `streambuf` 直接架在傳入的 buffer 上,不像 `write_buffer`
  那樣經過 `std::ostringstream`。`consumed` 由 `gptr() - eback()` 算出,精準。
- **不需要 `BUFFER_TOO_SMALL`**:那是 `write_buffer` 專屬的(往呼叫端 buffer 寫)。
  `read_buffer` 是讀進 `inst`,沒有這個問題。
- **不需要 `ferror` 補判**:記憶體來源不會有 I/O 錯誤;`read` 那個補判是為了 FILE。

## 簽章(加在 `include/aos/aos.h`)

```c
/*
 * 從記憶體裡的一段位元組讀下一筆,是 aos_instruction_read 的 FILE-free 版本,
 * 好讓不方便造出 FILE* 的呼叫端(尤其是其他語言的綁定)也能讀。
 *
 * 從 buffer[0] 開始讀一筆,把實際吃掉的位元組數寫進 *consumed —— 呼叫端據此
 * 前進讀下一筆。回傳值與 aos_instruction_read 完全一樣(含 AOS_INST_EOF)。
 *
 * consumed 可以傳 NULL。任何失敗(含 EOF)都會讓 inst 變成空的,規則同 read。
 */
AOS_API aos_inst_state aos_instruction_read_buffer(const char *buffer,
                                                   size_t size,
                                                   aos_instruction *inst,
                                                   size_t max_record_bytes,
                                                   size_t *consumed);
```

- 參數順序**比照 `read`**:來源(buffer+size)在前,再 `inst`、`max_record_bytes`,
  out 參數 `consumed` 殿後。
- `AOS_API` 一定要有,否則不會出現在共享函式庫的符號表。

## 實作(`src/capi.cpp`)

在匿名 namespace 裡加一個唯讀的 membuf(放在 `FileBuf` 旁邊):

```cpp
/*
 * 唯讀地把一段記憶體接成 streambuf。全部內容一開始就在 get 區,不需要
 * underflow;consumed() 回報已經讀過幾個位元組,給 read_buffer 前進用。
 */
class MemBuf : public std::streambuf {
public:
    MemBuf(const char *data, std::size_t size)
    {
        char *p = const_cast<char *>(data);   /* streambuf 的 get 區要 char* */
        setg(p, p, p + size);
    }
    std::size_t consumed() const
    {
        return static_cast<std::size_t>(gptr() - eback());
    }
};
```

進入點(放在 `aos_instruction_read` 後面):

```cpp
aos_inst_state aos_instruction_read_buffer(const char *buffer, size_t size,
                                           aos_instruction *inst,
                                           size_t max_record_bytes,
                                           size_t *consumed)
{
    if (consumed != nullptr) {
        *consumed = 0;
    }
    if (buffer == nullptr || inst == nullptr) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    try {
        MemBuf buf(buffer, size);
        std::istream in(&buf);
        const aos::InstState state =
            aos::read_instruction(in, inst->inst, max_record_bytes);

        if (consumed != nullptr) {
            *consumed = buf.consumed();
        }
        return static_cast<aos_inst_state>(static_cast<int>(state));
    } catch (...) {
        return AOS_INST_ALLOC_FAILED;
    }
}
```

- `const_cast` 是安全的:`MemBuf` 不覆寫任何寫入相關的虛擬函式,get 區純粹被讀。
- `size == 0`(buffer 非 NULL)時:空範圍 → `read_instruction` 讀第一行立刻 EOF →
  回 `AOS_INST_EOF`、`consumed == 0`。這正是「空輸入 = 乾淨結束」,不是錯誤。

## ABI 影響

- **只增不減**:新增一個函式是相容變更(見 capi.md 的 ABI 規則),soname 不動,
  不加任何列舉值,不動任何既有簽章。
- **不動版本字串**:是否把 `0.1.0` 往上帶由使用者決定,這份 spec 不改
  `aos_version_string`(先前 env/分隔兩次改動也沒動它)。

## 測試(`tests/test_capi.c`)

- **round-trip**:`write_buffer` 出一筆 → `read_buffer` 讀回來,argv/env/欄位都相等,
  且 `consumed` 等於 `write_buffer` 回報的 `needed`。
- **一個 buffer 兩筆**:讀第一筆拿到 `consumed=N1`,`buffer+=N1; size-=N1`,再讀第二
  筆,最後一次讀到 `AOS_INST_EOF`。
- **空 buffer**(`size==0`,buffer 非 NULL)→ `AOS_INST_EOF`,`consumed==0`。
- **NULL 參數**:`buffer==NULL` 或 `inst==NULL` → `AOS_INST_INVALID_ARGUMENT`。
- **格式錯誤**:第九行非空的一段 bytes → `AOS_INST_MISSING_SEPARATOR`。
- **`consumed` 可傳 NULL**:傳 NULL 不會 crash,功能照常。

## `.md` 文件(由主 agent 收尾,subagent 不要動 `.md`)

`docs/capi.md`(新增 read_buffer 的說明,點出它是 read 的 FILE-free 版、以及
「用 consumed 前進 loop 一段 buffer」的範例;並在 read 一節提一句有這個變體)、
`docs/architecture.md`(shim 一節:讀寫兩邊現在對稱,FILE-free 路徑補齊、C 進入點
數 21→22)、`docs/cxxapi.md`(對照表加一列「從 bytes 讀 | std::istringstream |
read_buffer」)。

## 驗證

- WSL Ubuntu:`make strict` 全綠、零警告。
  `wsl -d Ubuntu -e bash -lc 'cd /mnt/c/code/mine/simple_tools/aos-c && make strict'`。
- 完成後回報 `src/capi.cpp` 的大小(bytes)。
