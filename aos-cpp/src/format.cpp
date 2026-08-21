#include <aos/format.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <string_view>
#include <utility>

namespace aos {
namespace {

using json = nlohmann::json;

struct depth_exceeded {};

InstState validate(const inst_t &inst) {
    if (inst.argv.empty() || inst.argv.front().empty()) {
        return InstState::EmptyArgv;
    }
    if (inst.argv.size() > kMaxArgs) {
        return InstState::TooManyArgs;
    }
    if (inst.env.size() > kMaxEnv) {
        return InstState::TooManyEnv;
    }
    for (const auto &entry : inst.env) {
        if (entry.first.empty() || entry.first.find('=') != std::string::npos) {
            return InstState::EnvKeyInvalid;
        }
    }
    return InstState::Ok;
}

bool known_key(std::string_view key) {
    constexpr std::array<std::string_view, 8> keys = {
        "argv", "stdin", "stdout", "stderr", "exit", "cwd", "env",
        "timeout_ms",
    };
    for (const auto candidate : keys) {
        if (key == candidate) {
            return true;
        }
    }
    return false;
}

InstState decode(const json &value, inst_t &inst) {
    if (!value.is_object()) {
        return InstState::NotAnObject;
    }
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!known_key(it.key())) {
            return InstState::UnknownKey;
        }
    }

    const auto argv_it = value.find("argv");
    if (argv_it == value.end()) {
        return InstState::EmptyArgv;
    }
    if (!argv_it->is_array()) {
        return InstState::FieldTypeMismatch;
    }
    if (argv_it->size() > kMaxArgs) {
        return InstState::TooManyArgs;
    }
    for (const auto &arg : *argv_it) {
        if (!arg.is_string()) {
            return InstState::FieldTypeMismatch;
        }
        inst.argv.push_back(arg.get<std::string>());
    }

    const std::pair<const char *, std::string *> strings[] = {
        {"stdin", &inst.stdin_path}, {"stdout", &inst.stdout_path},
        {"stderr", &inst.stderr_path}, {"exit", &inst.exit_path},
        {"cwd", &inst.cwd},
    };
    for (const auto &field : strings) {
        const auto it = value.find(field.first);
        if (it != value.end()) {
            if (!it->is_string()) {
                return InstState::FieldTypeMismatch;
            }
            *field.second = it->get<std::string>();
        }
    }

    const auto env_it = value.find("env");
    if (env_it != value.end()) {
        if (!env_it->is_object()) {
            return InstState::FieldTypeMismatch;
        }
        if (env_it->size() > kMaxEnv) {
            return InstState::TooManyEnv;
        }
        for (auto it = env_it->begin(); it != env_it->end(); ++it) {
            if (!it.value().is_string()) {
                return InstState::FieldTypeMismatch;
            }
            if (it.key().empty() || it.key().find('=') != std::string::npos) {
                return InstState::EnvKeyInvalid;
            }
            inst.env.emplace(it.key(), it.value().get<std::string>());
        }
    }

    const auto timeout_it = value.find("timeout_ms");
    if (timeout_it != value.end()) {
        if (!timeout_it->is_number_unsigned()) {
            return InstState::FieldTypeMismatch;
        }
        inst.timeout_ms = timeout_it->get<std::uint64_t>();
    }
    return validate(inst);
}

}  // namespace

InstState read_one(const char *line, std::size_t size, inst_t &out,
                   const ReadOptions &opts) {
    out.clear();
    if (line == nullptr) {
        return InstState::InvalidArgument;
    }
    if (size > opts.max_total_bytes) {
        return InstState::TotalTooLong;
    }
    if (size > opts.max_record_bytes) {
        return InstState::RecordTooLong;
    }

    try {
        const auto callback = [](int depth, json::parse_event_t event, json &) {
            if ((event == json::parse_event_t::object_start ||
                 event == json::parse_event_t::array_start) &&
                depth >= static_cast<int>(kMaxJsonDepth)) {
                throw depth_exceeded{};
            }
            return true;
        };
        const json value = json::parse(line, line + size, callback, true, false);
        inst_t parsed;
        const InstState state = decode(value, parsed);
        if (state == InstState::Ok) {
            out = std::move(parsed);
        }
        return state;
    } catch (const depth_exceeded &) {
        return InstState::DepthExceeded;
    } catch (const json::parse_error &) {
        return InstState::JsonSyntax;
    }
}

InstState read_all(const char *data, std::size_t size,
                   std::vector<inst_t> &out, std::size_t *error_line,
                   const ReadOptions &opts) {
    out.clear();
    if (error_line != nullptr) {
        *error_line = 0;
    }
    if (data == nullptr) {
        return InstState::InvalidArgument;
    }
    if (size > opts.max_total_bytes) {
        return InstState::TotalTooLong;
    }

    std::vector<inst_t> parsed;
    std::size_t begin = 0;
    std::size_t line_number = 1;
    while (begin < size) {
        std::size_t end = begin;
        while (end < size && data[end] != '\n') {
            ++end;
        }
        std::size_t line_size = end - begin;
        if (line_size != 0 && data[begin + line_size - 1] == '\r') {
            --line_size;
        }
        if (line_size != 0) {
            inst_t inst;
            const InstState state = read_one(data + begin, line_size, inst, opts);
            if (state != InstState::Ok) {
                if (error_line != nullptr) {
                    *error_line = line_number;
                }
                return state;
            }
            parsed.push_back(std::move(inst));
        }
        begin = end == size ? size : end + 1;
        ++line_number;
    }
    out.swap(parsed);
    return InstState::Ok;
}

InstState write_one(const inst_t &inst, std::string &out) {
    const InstState state = validate(inst);
    if (state != InstState::Ok) {
        return state;
    }

    nlohmann::ordered_json value;
    value["argv"] = inst.argv;
    if (!inst.stdin_path.empty()) value["stdin"] = inst.stdin_path;
    if (!inst.stdout_path.empty()) value["stdout"] = inst.stdout_path;
    if (!inst.stderr_path.empty()) value["stderr"] = inst.stderr_path;
    if (!inst.exit_path.empty()) value["exit"] = inst.exit_path;
    if (!inst.cwd.empty()) value["cwd"] = inst.cwd;
    if (!inst.env.empty()) value["env"] = inst.env;
    if (inst.timeout_ms != 0) value["timeout_ms"] = inst.timeout_ms;

    std::string record = value.dump();
    record.push_back('\n');
    out.append(record);
    return InstState::Ok;
}

}  // namespace aos
