#include "run.hpp"

#include "aos/exec.hpp"
#include "aos/inst.hpp"

#include <fstream>
#include <iostream>
#include <string>

namespace aos {
namespace {

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
         * 一筆跑不起來不會停下整輪：後面的指令跟它沒有關係，跳過它們只是把
         * 一個失敗變成很多個沒做的事。子行程回傳非零更不算失敗，那是資料，
         * 該由 exit_path 記下來。
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

    std::ifstream file(argv[1]);

    if (!file) {
        std::cerr << "aos: could not open " << argv[1] << '\n';
        return 1;
    }
    return run_stream(file);
}

}  /* namespace aos */
