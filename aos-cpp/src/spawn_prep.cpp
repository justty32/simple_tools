#define _POSIX_C_SOURCE 200809L

#include "spawn_prep.hpp"

#include <cerrno>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

namespace aos::detail {
namespace {

constexpr int kExitSetupFailed = 126;
constexpr int kExitExecFailed = 127;

std::string current_directory() {
    std::vector<char> buffer(256);
    for (;;) {
        if (getcwd(buffer.data(), buffer.size()) != nullptr) {
            return buffer.data();
        }
        if (errno != ERANGE) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string join_path(const std::string &left, const std::string &right) {
    if (left.empty() || left.back() == '/') {
        return left + right;
    }
    return left + '/' + right;
}

std::string default_path() {
    const std::size_t size = confstr(_CS_PATH, nullptr, 0);
    if (size == 0) {
        return "/bin:/usr/bin";
    }
    std::vector<char> buffer(size);
    if (confstr(_CS_PATH, buffer.data(), buffer.size()) == 0) {
        return "/bin:/usr/bin";
    }
    return buffer.data();
}

void merge_environment(const inst_t &inst, SpawnPrep &prep,
                       const char *&path_value, bool &path_is_set) {
    for (const auto &entry : inst.env) {
        if (entry.first.empty() || entry.first.find('=') != std::string::npos) {
            prep.failure_status = kExitSetupFailed;
            return;
        }
    }

    std::map<std::string, bool> emitted;
    for (char **item = environ; item != nullptr && *item != nullptr; ++item) {
        const std::string inherited(*item);
        const std::size_t separator = inherited.find('=');
        if (separator == std::string::npos) {
            prep.environment.push_back(inherited);
            continue;
        }

        const std::string key = inherited.substr(0, separator);
        const auto replacement = inst.env.find(key);
        if (replacement == inst.env.end()) {
            prep.environment.push_back(inherited);
        } else if (!emitted[key]) {
            prep.environment.push_back(key + '=' + replacement->second);
            emitted[key] = true;
        }
    }
    for (const auto &entry : inst.env) {
        if (!emitted[entry.first]) {
            prep.environment.push_back(entry.first + '=' + entry.second);
        }
    }

    prep.envp.reserve(prep.environment.size() + 1);
    for (std::string &entry : prep.environment) {
        prep.envp.push_back(entry.data());
        if (!path_is_set && entry.compare(0, 5, "PATH=") == 0) {
            path_value = entry.c_str() + 5;
            path_is_set = true;
        }
    }
    prep.envp.push_back(nullptr);
}

std::string child_directory(const inst_t &inst) {
    const std::string parent = current_directory();
    if (parent.empty() || inst.cwd.empty()) {
        return parent;
    }
    if (inst.cwd.front() == '/') {
        return inst.cwd;
    }
    return join_path(parent, inst.cwd);
}

void resolve_executable(const inst_t &inst, const std::string &path,
                        SpawnPrep &prep) {
    const std::string base = child_directory(inst);
    if (base.empty()) {
        prep.failure_status = kExitExecFailed;
        return;
    }

    bool saw_eacces = false;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t end = path.find(':', begin);
        const std::string field = path.substr(begin, end - begin);
        const std::string directory = field.empty()
                                          ? base
                                          : (field.front() == '/'
                                                 ? field
                                                 : join_path(base, field));
        const std::string candidate = join_path(directory, inst.argv[0]);

        struct stat info {};
        if (stat(candidate.c_str(), &info) == 0) {
            if (S_ISREG(info.st_mode) && access(candidate.c_str(), X_OK) == 0) {
                prep.executable = candidate;
                return;
            }
            saw_eacces = true;
        } else if (errno == EACCES) {
            saw_eacces = true;
        }

        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    prep.failure_status = saw_eacces ? kExitSetupFailed : kExitExecFailed;
}

}  // namespace

void prepare_spawn(const inst_t &inst, SpawnPrep &prep) {
    const char *path_value = nullptr;
    bool path_is_set = false;
    merge_environment(inst, prep, path_value, path_is_set);
    if (prep.failure_status != 0) {
        return;
    }

    if (inst.argv[0].find('/') != std::string::npos) {
        prep.executable = inst.argv[0];
        return;
    }

    const std::string path = path_is_set ? path_value : default_path();
    resolve_executable(inst, path, prep);
}

}  // namespace aos::detail
