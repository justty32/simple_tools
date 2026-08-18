#include "test_common.hpp"

#include <sstream>
#include <string>
#include <vector>

/*
 * test_common.hpp 把 run_inst_read_tests() 宣告在全域範圍（供
 * test_main.cpp 呼叫），所以這裡引入 aos 而不是把整個檔案包進
 * namespace aos 裡。
 */
using namespace aos;

namespace {

/* 用定位字元組出 argv 行，不含結尾換行。 */
std::string join_tabs(const std::vector<std::string> &argv)
{
    std::string line;

    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            line += '\t';
        }
        line += argv[i];
    }
    return line;
}

/*
 * 組一筆完整記錄：argv 行加上其餘七行，各自以 eol 結尾。eol 讓同一個
 * 建構器可以同時產生 LF 與 CRLF 兩種輸入。
 */
std::string build_record(const std::string &argv_line,
                         const std::vector<std::string> &fields,
                         const std::string &eol = "\n")
{
    std::string record = argv_line + eol;

    for (const std::string &field : fields) {
        record += field + eol;
    }
    return record;
}

/* 後七行皆為同一值時常用的捷徑；空字串對每一行都是合法的。 */
std::vector<std::string> seven(const std::string &value)
{
    return std::vector<std::string>(7, value);
}

std::size_t test_single_valid_record()
{
    std::vector<std::string> argv = { "prog", "arg1", "arg2" };
    std::vector<std::string> fields = {
        "in.txt", "out.txt", "err.txt", "exit.txt",
        "/tmp/work", "PATH=/bin\tHOME=/root", "extra data"
    };
    std::istringstream in(build_record(join_tabs(argv), fields));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == 3);
    CHECK(inst.argv[0] == "prog");
    CHECK(inst.argv[1] == "arg1");
    CHECK(inst.argv[2] == "arg2");
    CHECK(inst.stdin_path == "in.txt");
    CHECK(inst.stdout_path == "out.txt");
    CHECK(inst.stderr_path == "err.txt");
    CHECK(inst.exit_path == "exit.txt");
    CHECK(inst.cwd == "/tmp/work");
    CHECK(inst.env.size() == 2);
    CHECK(inst.env[0] == "PATH=/bin");
    CHECK(inst.env[1] == "HOME=/root");
    CHECK(inst.extra == "extra data");
    return 1;
}

std::size_t test_argv_splitting()
{
    std::size_t cases = 0;

    /* 三個引數，皆以定位字元分隔。 */
    {
        std::istringstream in(build_record("a\tb\tc", seven("")));
        inst_t inst;

        CHECK(read_instruction(in, inst) == InstState::Ok);
        CHECK(inst.argv.size() == 3);
        CHECK(inst.argv[0] == "a");
        CHECK(inst.argv[1] == "b");
        CHECK(inst.argv[2] == "c");
        ++cases;
    }

    /* 相鄰定位字元保留中間的空引數。 */
    {
        std::istringstream in(build_record("a\t\tb", seven("")));
        inst_t inst;

        CHECK(read_instruction(in, inst) == InstState::Ok);
        CHECK(inst.argv.size() == 3);
        CHECK(inst.argv[0] == "a");
        CHECK(inst.argv[1] == "");
        CHECK(inst.argv[2] == "b");
        ++cases;
    }

    /* 結尾定位字元產生一個結尾空引數。 */
    {
        std::istringstream in(build_record("a\tb\t", seven("")));
        inst_t inst;

        CHECK(read_instruction(in, inst) == InstState::Ok);
        CHECK(inst.argv.size() == 3);
        CHECK(inst.argv[0] == "a");
        CHECK(inst.argv[1] == "b");
        CHECK(inst.argv[2] == "");
        ++cases;
    }

    return cases;
}

std::size_t test_single_argument_no_tab()
{
    std::istringstream in(build_record("solo", seven("")));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == 1);
    CHECK(inst.argv[0] == "solo");
    return 1;
}

std::size_t test_all_other_fields_empty()
{
    std::istringstream in(build_record("x", seven("")));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.stdin_path == "");
    CHECK(inst.stdout_path == "");
    CHECK(inst.stderr_path == "");
    CHECK(inst.exit_path == "");
    CHECK(inst.cwd == "");
    CHECK(inst.env.empty());
    CHECK(inst.extra == "");
    return 1;
}

/*
 * env 行與 argv 行的切法相同，差別只在空行的意義：argv 的空行是錯誤，env
 * 的空行是「沒有任何變數」，也就是繼承呼叫端的環境。
 */
std::size_t test_env_splitting()
{
    std::size_t cases = 0;

    /* 空行 = 空清單，而且是成功的讀取。 */
    {
        std::istringstream in(build_record("x", seven("")));
        inst_t inst;

        CHECK(read_instruction(in, inst) == InstState::Ok);
        CHECK(inst.env.empty());
        ++cases;
    }

    /* 一筆。 */
    {
        std::vector<std::string> fields = seven("");
        fields[5] = "K=v";
        std::istringstream in(build_record("x", fields));
        inst_t inst;

        CHECK(read_instruction(in, inst) == InstState::Ok);
        CHECK(inst.env.size() == 1);
        CHECK(inst.env[0] == "K=v");
        ++cases;
    }

    /* 多筆，含空值與含 '=' 的值：只有第一個 '=' 是分隔符號。 */
    {
        std::vector<std::string> fields = seven("");
        fields[5] = "A=1\tB=\tC=x=y";
        std::istringstream in(build_record("x", fields));
        inst_t inst;

        CHECK(read_instruction(in, inst) == InstState::Ok);
        CHECK(inst.env.size() == 3);
        CHECK(inst.env[0] == "A=1");
        CHECK(inst.env[1] == "B=");
        CHECK(inst.env[2] == "C=x=y");
        ++cases;
    }

    /* 值裡的空白、引號與反斜線都是普通字元，跟 argv 一樣。 */
    {
        std::vector<std::string> fields = seven("");
        fields[5] = "MSG=hello world\tQ=\"quoted\"";
        std::istringstream in(build_record("x", fields));
        inst_t inst;

        CHECK(read_instruction(in, inst) == InstState::Ok);
        CHECK(inst.env.size() == 2);
        CHECK(inst.env[0] == "MSG=hello world");
        CHECK(inst.env[1] == "Q=\"quoted\"");
        ++cases;
    }

    return cases;
}

std::size_t test_env_entry_must_be_key_value()
{
    /* 沒有 '='、鍵是空的、以及夾在合法項目之間的壞項目，全都是壞記錄。 */
    const char *const bad[] = { "NOEQUALS", "=value", "A=1\t=2", "A=1\tB" };
    std::size_t cases = 0;

    for (const char *entry : bad) {
        std::vector<std::string> fields = seven("");
        fields[5] = entry;
        std::istringstream in(build_record("x", fields));
        inst_t inst;

        CHECK(read_instruction(in, inst) == InstState::EnvEntryMalformed);
        /* 壞掉的一行不能留下半套 env，也不能留下已經讀好的其他欄位。 */
        CHECK(inst.empty());
        CHECK(inst.env.empty());
        ++cases;
    }
    return cases;
}

std::size_t test_crlf_strips_every_field()
{
    std::vector<std::string> argv = { "a", "b", "c" };
    std::vector<std::string> fields = {
        "in", "out", "err", "exit", "cwd", "E=v", "extra"
    };
    std::istringstream in(build_record(join_tabs(argv), fields, "\r\n"));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    /* CR 只會在行尾被剝除，這裡每一行都以 CRLF 結尾，所以全部欄位以及
     * 最後一個 argv 都不該留下 CR。 */
    CHECK(inst.argv.size() == 3);
    CHECK(inst.argv[0] == "a");
    CHECK(inst.argv[1] == "b");
    CHECK(inst.argv[2] == "c");
    CHECK(inst.stdin_path == "in");
    CHECK(inst.stdout_path == "out");
    CHECK(inst.stderr_path == "err");
    CHECK(inst.exit_path == "exit");
    CHECK(inst.cwd == "cwd");
    CHECK(inst.env.size() == 1);
    CHECK(inst.env[0] == "E=v");
    CHECK(inst.extra == "extra");
    return 1;
}

std::size_t test_bare_cr_in_middle_is_data()
{
    /* 位於行中間的 CR 不是 CRLF 的一部分，所以是資料而非行尾標記。 */
    std::vector<std::string> fields = seven("");
    fields[6] = "ab\rcd";
    std::istringstream in(build_record("x", fields));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.extra == "ab\rcd");
    return 1;
}

std::size_t test_two_records_reuse_instruction()
{
    std::vector<std::string> argv1 = { "first", "one" };
    std::vector<std::string> fields1 = {
        "a1", "b1", "c1", "d1", "e1", "F=1\tG=1", "g1"
    };
    std::vector<std::string> argv2 = { "second" };
    std::vector<std::string> fields2 = {
        "a2", "b2", "c2", "d2", "e2", "", "g2"
    };
    std::string data = build_record(join_tabs(argv1), fields1) +
                       build_record(join_tabs(argv2), fields2);
    std::istringstream in(data);
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == 2);
    CHECK(inst.argv[0] == "first");
    CHECK(inst.argv[1] == "one");
    CHECK(inst.stdin_path == "a1");
    CHECK(inst.env.size() == 2);
    CHECK(inst.extra == "g1");

    /* 第二次讀取必須完全取代第一筆記錄留下的內容。 */
    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == 1);
    CHECK(inst.argv[0] == "second");
    CHECK(inst.stdin_path == "a2");
    /* 前一筆的兩個 env 必須被這一筆的空 env 取代，而不是留下來。 */
    CHECK(inst.env.empty());
    CHECK(inst.extra == "g2");

    CHECK(read_instruction(in, inst) == InstState::Eof);
    CHECK(inst.empty());
    CHECK(inst.stdin_path == "");
    CHECK(inst.stdout_path == "");
    CHECK(inst.stderr_path == "");
    CHECK(inst.exit_path == "");
    CHECK(inst.cwd == "");
    CHECK(inst.env.empty());
    CHECK(inst.extra == "");
    return 1;
}

std::size_t test_ordinary_special_characters()
{
    std::vector<std::string> argv = { "a b", "\"quoted\"", "back\\slash" };
    std::istringstream in(build_record(join_tabs(argv), seven("")));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv[0] == "a b");
    CHECK(inst.argv[1] == "\"quoted\"");
    CHECK(inst.argv[2] == "back\\slash");
    return 1;
}

std::size_t test_to_c_argv()
{
    inst_t inst = make_inst({ "prog", "", "arg" });
    std::vector<char *> cargv = to_c_argv(inst);

    CHECK(cargv.size() == inst.argv.size() + 1);
    CHECK(cargv[cargv.size() - 1] == nullptr);
    for (std::size_t i = 0; i < inst.argv.size(); ++i) {
        CHECK(cargv[i] == &inst.argv[i][0]);
    }
    /* 空字串引數仍必須指向一個有效、指向 NUL 的指標。 */
    CHECK(cargv[1] != nullptr);
    CHECK(*cargv[1] == '\0');
    return 1;
}

std::size_t test_to_c_envp()
{
    std::size_t cases = 0;

    {
        inst_t inst = make_inst({ "prog" });

        inst.env = { "A=1", "B=2" };

        std::vector<char *> envp = to_c_envp(inst);

        CHECK(envp.size() == inst.env.size() + 1);
        CHECK(envp[envp.size() - 1] == nullptr);
        for (std::size_t i = 0; i < inst.env.size(); ++i) {
            CHECK(envp[i] == &inst.env[i][0]);
        }
        ++cases;
    }

    /* 空的 env 攤出來就只有結尾的空指標，仍然是可以直接給 environ 的形狀。 */
    {
        inst_t inst = make_inst({ "prog" });
        std::vector<char *> envp = to_c_envp(inst);

        CHECK(envp.size() == 1);
        CHECK(envp[0] == nullptr);
        ++cases;
    }

    return cases;
}

}  /* namespace */

std::size_t run_inst_read_tests()
{
    std::size_t count = 0;

    count += test_single_valid_record();
    count += test_argv_splitting();
    count += test_single_argument_no_tab();
    count += test_all_other_fields_empty();
    count += test_env_splitting();
    count += test_env_entry_must_be_key_value();
    count += test_crlf_strips_every_field();
    count += test_bare_cr_in_middle_is_data();
    count += test_two_records_reuse_instruction();
    count += test_ordinary_special_characters();
    count += test_to_c_argv();
    count += test_to_c_envp();
    return count;
}
