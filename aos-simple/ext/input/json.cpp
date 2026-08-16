#include "json.hpp"

#include <cstdint>

namespace aosinput {

JsonValue JsonValue::of_string(std::string s) {
    JsonValue v;
    v.is_string_ = true;
    v.string_ = std::move(s);
    return v;
}

JsonValue JsonValue::of_array(std::vector<std::string> a) {
    JsonValue v;
    v.is_string_ = false;
    v.array_ = std::move(a);
    return v;
}

namespace {

class Parser {
  public:
    Parser(const std::string &text, std::string &error) : s_(text), error_(error) {}

    bool object(JsonObject &out) {
        skip_space();
        if (!eat('{')) return fail("開頭要是 {");
        skip_space();
        if (eat('}')) return tail_clean();

        for (;;) {
            skip_space();
            std::string key;
            if (!string_value(key)) return false;
            if (out.count(key) != 0) return fail("重複的 key：" + key);

            skip_space();
            if (!eat(':')) return fail("key 之後要是 :");
            skip_space();

            JsonValue value;
            if (!any_value(value)) return false;
            out.emplace(std::move(key), std::move(value));

            skip_space();
            if (eat(',')) continue;
            if (eat('}')) return tail_clean();
            return fail("值之後要是 , 或 }");
        }
    }

  private:
    // ★ 只有兩種值。看到別的就明說，不要含糊帶過。
    bool any_value(JsonValue &out) {
        if (peek() == '"') {
            std::string s;
            if (!string_value(s)) return false;
            out = JsonValue::of_string(std::move(s));
            return true;
        }
        if (peek() == '[') {
            std::vector<std::string> items;
            if (!string_array(items)) return false;
            out = JsonValue::of_array(std::move(items));
            return true;
        }
        return fail("值只能是字串或字串陣列");
    }

    bool string_array(std::vector<std::string> &out) {
        if (!eat('[')) return fail("陣列要以 [ 開頭");
        skip_space();
        if (eat(']')) return true;
        for (;;) {
            skip_space();
            std::string item;
            if (!string_value(item)) return fail("陣列裡只能放字串");
            out.push_back(std::move(item));
            skip_space();
            if (eat(',')) continue;
            if (eat(']')) return true;
            return fail("陣列元素之後要是 , 或 ]");
        }
    }

    bool string_value(std::string &out) {
        if (!eat('"')) return fail("這裡要是一個字串");
        out.clear();
        for (;;) {
            if (at_end()) return fail("字串沒有收尾的引號");
            const char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                // ⚠ 未跳脫的控制字元照 JSON 是非法的。擋掉，否則一行一個物件的
                //   格式會被一個裸換行整個弄壞。
                if (static_cast<unsigned char>(c) < 0x20) {
                    return fail("字串裡有沒跳脫的控制字元");
                }
                out.push_back(c);
                continue;
            }
            if (at_end()) return fail("反斜線後面沒東西");
            const char e = s_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    // ★ surrogate pair。少了這段，BMP 以外的字（emoji）會壞掉。
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (!(i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u')) {
                            return fail("high surrogate 後面要接 \\u low surrogate");
                        }
                        i_ += 2;
                        std::uint32_t low = 0;
                        if (!hex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return fail("不合法的 low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("落單的 low surrogate");
                    }
                    encode_utf8(cp, out);
                    break;
                }
                default: return fail("不認得的跳脫序列");
            }
        }
    }

    bool hex4(std::uint32_t &out) {
        if (i_ + 4 > s_.size()) return fail("\\u 後面要有四位十六進位");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return fail("\\u 後面不是十六進位");
        }
        return true;
    }

    static void encode_utf8(std::uint32_t cp, std::string &out) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool tail_clean() {
        skip_space();
        if (!at_end()) return fail("} 後面還有東西");
        return true;
    }

    void skip_space() {
        while (!at_end() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) {
            ++i_;
        }
    }
    bool at_end() const { return i_ >= s_.size(); }
    char peek() const { return at_end() ? '\0' : s_[i_]; }
    bool eat(char c) {
        if (peek() != c) return false;
        ++i_;
        return true;
    }
    bool fail(const std::string &why) {
        error_ = why + "（位置 " + std::to_string(i_) + "）";
        return false;
    }

    const std::string &s_;
    std::string &error_;
    std::size_t i_ = 0;
};

}  // namespace

bool parse_object(const std::string &text, JsonObject &out, std::string &error) {
    out.clear();
    error.clear();
    Parser parser{text, error};
    return parser.object(out);
}

}  // namespace aosinput
