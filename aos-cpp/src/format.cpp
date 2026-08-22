#include <aos/format.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <string_view>
#include <utility>

namespace aos {
namespace {

using json = nlohmann::json;

json parse_json(const char *data, std::size_t size) {
    return json::parse(data, data + size);
}

InstState validate(const inst_t &inst) {
    if (inst.argv.empty() || inst.argv.front().empty()) {
        return InstState::EmptyArgv;
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

nlohmann::ordered_json encode(const inst_t &inst) {
    nlohmann::ordered_json value;
    value["argv"] = inst.argv;
    if (!inst.stdin_path.empty()) value["stdin"] = inst.stdin_path;
    if (!inst.stdout_path.empty()) value["stdout"] = inst.stdout_path;
    if (!inst.stderr_path.empty()) value["stderr"] = inst.stderr_path;
    if (!inst.exit_path.empty()) value["exit"] = inst.exit_path;
    if (!inst.cwd.empty()) value["cwd"] = inst.cwd;
    if (!inst.env.empty()) value["env"] = inst.env;
    if (inst.timeout_ms != 0) value["timeout_ms"] = inst.timeout_ms;
    return value;
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

InstState read_one(const char *data, std::size_t size, inst_t &out) {
    out.clear();
    if (data == nullptr) {
        return InstState::InvalidArgument;
    }

    try {
        const json value = parse_json(data, size);
        inst_t parsed;
        const InstState state = decode(value, parsed);
        if (state == InstState::Ok) {
            out = std::move(parsed);
        }
        return state;
    } catch (const json::parse_error &) {
        return InstState::JsonSyntax;
    }
}

InstState read_all(const char *data, std::size_t size,
                   std::vector<inst_t> &out, std::size_t *error_record) {
    out.clear();
    if (error_record != nullptr) {
        *error_record = 0;
    }
    if (data == nullptr) {
        return InstState::InvalidArgument;
    }

    try {
        const json document = parse_json(data, size);
        const bool single = document.is_object();
        if (!single && !document.is_array()) {
            return InstState::NotAnObject;
        }

        const std::size_t count = single ? 1 : document.size();
        std::vector<inst_t> parsed;
        parsed.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const json &value = single ? document : document[index];
            if (error_record != nullptr) {
                *error_record = index + 1;
            }
            inst_t inst;
            const InstState state = decode(value, inst);
            if (state != InstState::Ok) {
                return state;
            }
            parsed.push_back(std::move(inst));
        }
        out.swap(parsed);
        if (error_record != nullptr) {
            *error_record = 0;
        }
        return InstState::Ok;
    } catch (const json::parse_error &) {
        return InstState::JsonSyntax;
    }
}

InstState write_one(const inst_t &inst, std::string &out) {
    const InstState state = validate(inst);
    if (state != InstState::Ok) {
        return state;
    }

    std::string record = encode(inst).dump();
    record.push_back('\n');
    out.append(record);
    return InstState::Ok;
}

InstState write_all(const std::vector<inst_t> &insts, std::string &out,
                    std::size_t *error_record) {
    if (error_record != nullptr) {
        *error_record = 0;
    }
    for (std::size_t index = 0; index < insts.size(); ++index) {
        const InstState state = validate(insts[index]);
        if (state != InstState::Ok) {
            if (error_record != nullptr) {
                *error_record = index + 1;
            }
            return state;
        }
    }

    nlohmann::ordered_json document = nlohmann::ordered_json::array();
    for (const auto &inst : insts) {
        document.push_back(encode(inst));
    }
    std::string batch = document.dump();
    batch.push_back('\n');
    out.append(batch);
    return InstState::Ok;
}

}  // namespace aos
