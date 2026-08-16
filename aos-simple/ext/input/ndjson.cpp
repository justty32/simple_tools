#include "ndjson.hpp"

#include "json.hpp"

namespace aosinput {
namespace {

using aossimple::Call;

// JSON key -> Call 的哪個欄位。★ 這張表就是全部的合法 key。
struct Field {
    const char *key;
    std::string Call::*member;  // 必填的五個路徑
    bool required;
};

const Field kStringFields[] = {
    {"stdin", &Call::stdin_path, true},   {"stdout", &Call::stdout_path, true},
    {"stderr", &Call::stderr_path, true}, {"exit", &Call::exit_path, true},
    {"cwd", &Call::cwd, true},
};

bool known_key(const std::string &key) {
    if (key == "argv" || key == "env" || key == "user") return true;
    for (const Field &f : kStringFields) {
        if (key == f.key) return true;
    }
    return false;
}

}  // namespace

ParsedLine parse_call_line(const std::string &line) {
    ParsedLine result;

    JsonObject object;
    std::string why;
    if (!parse_object(line, object, why)) {
        result.error = "JSON 壞了：" + why;
        return result;
    }

    // ★★ 先擋不認得的 key。放在最前面是因為打錯字最常見，而且早報早好。
    for (const auto &entry : object) {
        if (!known_key(entry.first)) {
            result.error = "不認得的欄位：" + entry.first;
            return result;
        }
    }

    // argv：唯一的陣列欄位
    const auto argv_it = object.find("argv");
    if (argv_it == object.end()) {
        result.error = "少了 argv";
        return result;
    }
    if (!argv_it->second.is_array()) {
        result.error = "argv 要是字串陣列";
        return result;
    }
    result.call.argv = argv_it->second.array();

    // 五個必填路徑
    for (const Field &f : kStringFields) {
        const auto it = object.find(f.key);
        if (it == object.end()) {
            result.error = std::string{"少了 "} + f.key;
            return result;
        }
        if (!it->second.is_string()) {
            result.error = std::string{f.key} + " 要是字串";
            return result;
        }
        result.call.*(f.member) = it->second.string();
    }

    // 兩個可選的
    const auto env_it = object.find("env");
    if (env_it != object.end()) {
        if (!env_it->second.is_string()) {
            result.error = "env 要是字串";
            return result;
        }
        result.call.env = env_it->second.string();
    }
    const auto user_it = object.find("user");
    if (user_it != object.end()) {
        if (!user_it->second.is_string()) {
            result.error = "user 要是字串";
            return result;
        }
        result.call.user = user_it->second.string();
    }

    // ★ 形狀驗證在這裡就做掉，壞的請求根本不用進 queue。
    //   ⚠ 但這只是**共通**形狀（絕對路徑、沒有 NUL…）；「argv[0] 該長什麼樣」
    //     仍然是目的地的事，所以這裡不管它。
    const aossimple::Check check = aossimple::validate(result.call);
    if (!check) {
        result.error = check.reason;
        return result;
    }

    result.ok = true;
    return result;
}

ReadStats read_ndjson(
    std::istream &in, const std::function<void(Call)> &on_call,
    const std::function<void(unsigned long long, const std::string &)> &on_error) {
    ReadStats stats;
    std::string line;
    unsigned long long number = 0;

    while (std::getline(in, line)) {
        ++number;

        // 收尾的 \r，這樣 CRLF 的檔也讀得動。
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // 空行與 # 註解跳過。方便手寫測試檔，而且不算成錯。
        std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            ++stats.blank;
            continue;
        }

        ParsedLine parsed = parse_call_line(line);
        if (!parsed.ok) {
            // ★ 壞行不中斷讀取。報掉，下一行照常。
            ++stats.rejected;
            if (on_error) on_error(number, parsed.error);
            continue;
        }
        ++stats.accepted;
        if (on_call) on_call(std::move(parsed.call));
    }
    return stats;
}

}  // namespace aosinput
