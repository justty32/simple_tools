#include "templates.hpp"

// Templates live as real, editable files under templates/ and are embedded
// into the binary at compile time via #embed (GCC, C++20). The trailing 0
// turns each byte array into a C string. Project name is substituted at
// runtime by replacing the @NAME@ placeholder.
namespace dcap {
namespace {

constexpr char kCppMakefile[] = {
#embed "../templates/cpp/Makefile"
    , 0};
constexpr char kCppMain[] = {
#embed "../templates/cpp/main.cpp"
    , 0};
constexpr char kCMakefile[] = {
#embed "../templates/c/Makefile"
    , 0};
constexpr char kCMain[] = {
#embed "../templates/c/main.c"
    , 0};
constexpr char kGitignore[] = {
#embed "../templates/gitignore"
    , 0};

std::string subst_name(const char* tmpl, const std::string& name) {
    std::string s(tmpl);
    const std::string key = "@NAME@";
    for (size_t p = s.find(key); p != std::string::npos;
         p = s.find(key, p + name.size()))
        s.replace(p, key.size(), name);
    return s;
}

} // namespace

std::string cpp_makefile(const std::string& name) { return subst_name(kCppMakefile, name); }
std::string cpp_main() { return kCppMain; }
std::string cpp_gitignore() { return kGitignore; }

std::string c_makefile(const std::string& name) { return subst_name(kCMakefile, name); }
std::string c_main() { return kCMain; }
std::string c_gitignore() { return kGitignore; }

} // namespace dcap
