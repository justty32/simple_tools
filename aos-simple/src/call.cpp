#include "call.hpp"

namespace aossimple {
namespace {

bool has_nul(const std::string &s) { return s.find('\0') != std::string::npos; }

Check check_path(const std::string &value, const char *field) {
    if (value.empty()) return Check::fail(std::string{field} + " 不能是空的");
    if (has_nul(value)) return Check::fail(std::string{field} + " 不能含 NUL");
    if (value[0] != '/') {
        return Check::fail(std::string{field} + " 要是絕對路徑，拿到 " + value);
    }
    return Check::pass();
}

}  // namespace

Check validate(const Call &call) {
    if (call.argv.empty()) return Check::fail("argv 不能是空的");

    for (std::size_t i = 0; i < call.argv.size(); ++i) {
        if (has_nul(call.argv[i])) {
            // execve 用 NUL 結尾傳參數，帶不過去。與其讓它被安靜截斷，不如拒絕。
            return Check::fail("argv[" + std::to_string(i) + "] 不能含 NUL");
        }
    }
    // argv[0] 不必是絕對路徑——含 '/' 就直接用，不含就走 PATH 查找
    // （用的是這次呼叫**最終那份環境**的 PATH，見 exec.cpp）。
    // 只擋空字串：那顯然是填錯，查找一個空名字只會拿到一個難懂的 ENOENT。
    if (call.argv[0].empty()) return Check::fail("argv[0] 不能是空字串");

    const std::pair<const std::string *, const char *> paths[] = {
        {&call.stdin_path, "stdin"}, {&call.stdout_path, "stdout"},
        {&call.stderr_path, "stderr"}, {&call.exit_path, "exit"},
        {&call.cwd, "cwd"},
    };
    for (const auto &entry : paths) {
        const Check result = check_path(*entry.first, entry.second);
        if (!result) return result;
    }

    // env 給了就比照路徑辦理（存不存在不是這裡的事）。
    if (call.env) {
        const Check result = check_path(*call.env, "env");
        if (!result) return result;
    }

    // user 是不透明標示符，不是路徑：不要求絕對、不要求存在，只擋 NUL。
    if (call.user && has_nul(*call.user)) return Check::fail("user 不能含 NUL");

    return Check::pass();
}

}  // namespace aossimple
