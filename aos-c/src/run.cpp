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

/*
 * 這筆記錄雖然解析失敗，但串流位置還對得上下一筆嗎？
 *
 * EmptyArgv 和 TooManyArgs 是把整整八行都讀完之後才判定的，所以游標正好停在
 * 下一筆的開頭，跳過這筆就好。其餘的失敗都是在記錄中途放棄的 —— TooLong 是
 * 讀到一半的行就停手，Incomplete 是串流本身沒了 —— 位置已經無從對齊，再讀
 * 下去只會把後面每一筆都解成垃圾，那比直接停下來更糟。
 */
bool stream_still_aligned(InstState state)
{
    return state == InstState::EmptyArgv || state == InstState::TooManyArgs;
}

int run_stream(std::istream &in)
{
    Instruction inst;
    std::size_t index = 0;
    std::size_t failed = 0;

    for (;;) {
        const InstState state = read_instruction(in, inst);

        if (state == InstState::Eof) {
            break;
        }

        if (state != InstState::Ok) {
            report(index, "could not read", to_string(state));
            ++index;
            ++failed;
            if (stream_still_aligned(state)) {
                continue;
            }
            std::cerr << "aos: cannot resynchronise after that; "
                         "the remaining input is not read\n";
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
