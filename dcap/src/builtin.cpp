#include "builtin.hpp"

namespace dcap {

bool is_builtin(const std::string& name) {
    return name == "c" || name == "cpp";
}

bool write_builtin(const std::string& name, const std::string& dest) {
    if (name == "cpp")
        return write_builtin_cpp(dest);
    if (name == "c")
        return write_builtin_c(dest);
    return false;
}

} // namespace dcap
