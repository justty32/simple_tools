#include "aos/aos.hpp"

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
    Instruction inst;
    std::size_t index = 0;

    for (;;) {
        const InstState state = read_instruction(in, inst);

        if (state == InstState::Eof) {
            return 0;
        }
        if (state != InstState::Ok) {
            report(index, "could not read", to_string(state));
            return 1;
        }

        ExecResult result;
        const ExecState exec_state = execute(inst, result);

        if (exec_state != ExecState::Ok) {
            report(index, "could not run", to_string(exec_state));
            return 1;
        }
        /*
         * 子行程回傳非零不算執行失敗：那是資料，該由 exit_path 記下來，
         * 而不是中止後面的指令。
         */
        ++index;
    }
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
