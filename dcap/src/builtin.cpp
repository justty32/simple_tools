#include "builtin.hpp"

// The built-in template registry, by hand. #embed is a preprocessor directive
// and so cannot be wrapped in a macro: every file gets its own block, and the
// paths are relative to this file. Adding a file to a template means adding a
// block plus a kFiles entry; adding a template means adding a namespace plus a
// kTemplates entry.
//
// Each array gets a trailing 0 — an empty file would otherwise be an invalid
// initialiser — and the length is `sizeof - 1`, so the terminator never becomes
// part of the content. The arrays are unsigned char because template files are
// UTF-8: a byte above 127 would not fit a plain char.

namespace dcap {
namespace {

// The tables below cannot be constexpr because reinterpret_cast is not a
// constant expression; they are const with static initialisation instead.
std::string_view bytes(const unsigned char* p, std::size_t n) {
    return {reinterpret_cast<const char*>(p), n};
}

namespace tpl_c {

constexpr unsigned char cmakelists[] = {
#embed "../templates/c/CMakeLists.txt"
, 0};
constexpr unsigned char gitignore[] = {
#embed "../templates/c/.gitignore"
, 0};
constexpr unsigned char include_lib_h[] = {
#embed "../templates/c/include/lib.h"
, 0};
constexpr unsigned char src_lib_c[] = {
#embed "../templates/c/src/lib.c"
, 0};
constexpr unsigned char src_main_c[] = {
#embed "../templates/c/src/main.c"
, 0};

const File kFiles[] = {
    {"CMakeLists.txt", bytes(cmakelists, sizeof cmakelists - 1)},
    {".gitignore", bytes(gitignore, sizeof gitignore - 1)},
    {"include/lib.h", bytes(include_lib_h, sizeof include_lib_h - 1)},
    {"src/lib.c", bytes(src_lib_c, sizeof src_lib_c - 1)},
    {"src/main.c", bytes(src_main_c, sizeof src_main_c - 1)},
};

} // namespace tpl_c

namespace tpl_cpp {

constexpr unsigned char cmakelists[] = {
#embed "../templates/cpp/CMakeLists.txt"
, 0};
constexpr unsigned char gitignore[] = {
#embed "../templates/cpp/.gitignore"
, 0};
constexpr unsigned char include_lib_hpp[] = {
#embed "../templates/cpp/include/lib.hpp"
, 0};
constexpr unsigned char src_lib_cpp[] = {
#embed "../templates/cpp/src/lib.cpp"
, 0};
constexpr unsigned char src_main_cpp[] = {
#embed "../templates/cpp/src/main.cpp"
, 0};

const File kFiles[] = {
    {"CMakeLists.txt", bytes(cmakelists, sizeof cmakelists - 1)},
    {".gitignore", bytes(gitignore, sizeof gitignore - 1)},
    {"include/lib.hpp", bytes(include_lib_hpp, sizeof include_lib_hpp - 1)},
    {"src/lib.cpp", bytes(src_lib_cpp, sizeof src_lib_cpp - 1)},
    {"src/main.cpp", bytes(src_main_cpp, sizeof src_main_cpp - 1)},
};

} // namespace tpl_cpp

const Template kTemplates[] = {
    {"c", tpl_c::kFiles},
    {"cpp", tpl_cpp::kFiles},
};

} // namespace

std::optional<TemplateRef> find_builtin(std::string_view name) {
    for (const Template& t : kTemplates)
        if (name == t.name)
            return t;
    return std::nullopt;
}

std::string builtin_names() {
    std::string out;
    for (const Template& t : kTemplates) {
        if (!out.empty())
            out += " | ";
        out += t.name;
    }
    return out;
}

} // namespace dcap
