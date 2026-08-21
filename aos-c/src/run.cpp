/* 要在任何標頭之前宣告，理由與 exec.cpp 相同：-std=c++11 會定義
 * __STRICT_ANSI__ 而藏起 POSIX 介面。 */
#define _POSIX_C_SOURCE 200809L

#include "run.hpp"

#include "aos/exec.hpp"
#include "aos/inst.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <istream>
#include <streambuf>
#include <string>

#include <fcntl.h>
#include <unistd.h>

namespace aos {
namespace {

/*
 * 把一個 fd 接成 istream 的來源。
 *
 * 需要它的原因只有一個：insts 這條讀取路徑必須自己 open()，而 std::ifstream
 * 沒有可攜的辦法設定 open() 的旗標（見 open_insts）。既然檔案自己開，就得
 * 自己接上串流。
 *
 * 這裡自帶一塊緩衝，一次 read() 拿一批 —— 逐字元一次系統呼叫在 FIFO 上會很
 * 難看。串流語意不受影響：read() 有多少給多少，不會為了填滿緩衝區而多等。
 */
class FdBuf : public std::streambuf {
public:
    explicit FdBuf(int fd) : fd_(fd), error_(0) {}

    /* read() 真的出錯時的 errno；0 代表只是正常讀到結尾。 */
    int error() const { return error_; }

protected:
    int_type underflow() override
    {
        ssize_t got;

        do {
            got = read(fd_, buffer_, sizeof(buffer_));
        } while (got < 0 && errno == EINTR);

        if (got < 0) {
            /*
             * streambuf 沒有辦法把這個錯誤變成 badbit，讀取端只會看到沒有
             * 更多位元組。記下來讓呼叫端補報，否則 I/O 錯誤會被誤當成輸入
             * 正常結束。
             */
            error_ = errno;
            return traits_type::eof();
        }
        if (got == 0) {
            return traits_type::eof();
        }
        setg(buffer_, buffer_, buffer_ + got);
        return traits_type::to_int_type(buffer_[0]);
    }

private:
    int fd_;
    int error_;
    char buffer_[4096];
};

/*
 * 開啟 insts 的來源。回傳 -1 代表失敗，errno 留給呼叫端印訊息。
 *
 * 路徑可以是普通檔，也可以是 named FIFO —— 兩個旗標就是為了後者：
 *
 *   O_CLOEXEC   這個 fd 絕對不能漏進子行程。走 FIFO 時漏了，上游會永遠看到
 *               「這條管線還有讀者」，即使 aos-c 早就結束，於是等不到 EOF
 *               而可能無限阻塞。fork 複製的 fd 預設會活過 exec，所以這件事
 *               必須在 open 的時候就講清楚。
 *   O_NONBLOCK  開一條當下沒有寫入端的 FIFO 時，一般開法會卡在 open() 本身。
 *               加上它就立刻成功。
 */
int open_insts(const char *path)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);

    if (fd < 0) {
        return -1;
    }

    /*
     * 開完立刻把 O_NONBLOCK 清掉，讓後續的 read 回到阻塞語意 —— 那才是這裡
     * 要的：沒有寫入端就立刻 EOF（這次觸發沒事做，秒退）、有寫入端但還沒有
     * 資料就等、寫入端全關就 EOF（排空完成）。普通檔案上這個旗標無作用。
     */
    const int flags = fcntl(fd, F_GETFL);

    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        const int saved = errno;

        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

/* 錯誤訊息都附上記錄序號，因為串流讀取沒有其他方式指出是哪一筆出事。 */
void report(std::size_t index, const char *what, const char *detail)
{
    std::cerr << "aos: instruction " << index << ": " << what << ": " << detail
              << '\n';
}

int run_stream(std::istream &in)
{
    inst_t inst;
    std::size_t index = 0;
    std::size_t failed = 0;

    for (;;) {
        const InstState state = read_instruction(in, inst);

        if (state == InstState::Eof) {
            break;
        }

        /*
         * 解析失敗一律停止，即使游標剛好停在八行邊界上。
         *
         * 這個格式的記錄之間沒有分隔符號，所以「八行邊界」只是相對於已經讀
         * 過的內容而言，不代表檔案本身沒有錯位。而且寫入端根本不會產出空的
         * argv 或超量引數 —— 檔案裡出現這些，幾乎必然表示某一筆少了或多了
         * 行，後面每一筆都跟著位移。位移之後的記錄語法完全合法，只是內容來
         * 自別筆的欄位：繼續讀下去等於執行沒有人寫過的指令。
         */
        if (state != InstState::Ok) {
            report(index, "could not read", to_string(state));
            std::cerr << "aos: the file is not what it claims to be; "
                         "the remaining input is not read\n";
            ++index;
            ++failed;
            break;
        }

        ExecResult result;
        const ExecState exec_state = execute(inst, result);

        /*
         * 到這裡的失敗只剩 fork、wait、寫 exit 檔這種真正的執行期錯誤：找
         * 不到指令、重導向開不起來、cwd 不存在，現在都是一筆跑完並產出 127
         * 或 126 的記錄，不是失敗。子行程回傳非零同樣是資料，該由 exit_path
         * 記下來。
         *
         * 就算失敗也不停下整輪：後面的指令跟這一筆沒有關係，跳過它們只是把
         * 一個失敗變成很多個沒做的事。
         */
        if (exec_state != ExecState::Ok) {
            report(index, "could not run", to_string(exec_state));
            ++failed;
        }
        ++index;
    }

    if (failed > 0) {
        std::cerr << "aos: " << failed << " of " << index
                  << " instructions failed\n";
        return 1;
    }
    return 0;
}

}  /* namespace */

int run(int argc, char *argv[])
{
    if (argc > 2) {
        std::cerr << "usage: aos-c [instruction-file]\n";
        return 2;
    }
    if (argc < 2) {
        return run_stream(std::cin);
    }

    const int fd = open_insts(argv[1]);

    if (fd < 0) {
        std::cerr << "aos: could not open " << argv[1] << ": "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    FdBuf buf(fd);
    std::istream in(&buf);
    const int status = run_stream(in);

    close(fd);
    if (buf.error() != 0) {
        std::cerr << "aos: could not read " << argv[1] << ": "
                  << std::strerror(buf.error()) << '\n';
        return 1;
    }
    return status;
}

}  /* namespace aos */
