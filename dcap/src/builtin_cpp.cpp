#include "builtin.hpp"
#include "util.hpp"

namespace dcap {
namespace {
// unsigned char: template files may contain non-ASCII (UTF-8) bytes, which
// would narrow when stored in a signed char[] braced initializer.
constexpr unsigned char kMakefile[] = {
#embed "../templates/cpp/Makefile"
    , 0};
constexpr unsigned char kLibH[] = {
#embed "../templates/cpp/include/lib.hpp"
    , 0};
constexpr unsigned char kLibCpp[] = {
#embed "../templates/cpp/src/lib.cpp"
    , 0};
constexpr unsigned char kMainCpp[] = {
#embed "../templates/cpp/src/main.cpp"
    , 0};
constexpr unsigned char kTestCpp[] = {
#embed "../templates/cpp/test/test.cpp"
    , 0};
constexpr unsigned char kReadme[] = {
#embed "../templates/cpp/README.md"
    , 0};
constexpr unsigned char kFmt[] = {
#embed "../templates/cpp/.clang-format"
    , 0};
constexpr unsigned char kGit[] = {
#embed "../templates/cpp/.gitignore"
    , 0};

bool put(const std::string& path, const unsigned char* data) {
    return write_file(path, reinterpret_cast<const char*>(data));
}
} // namespace

bool write_builtin_cpp(const std::string& d) {
    bool ok = ensure_dir(d + "/include") && ensure_dir(d + "/src") &&
              ensure_dir(d + "/test");
    ok = put(d + "/Makefile", kMakefile) && ok;
    ok = put(d + "/include/lib.hpp", kLibH) && ok;
    ok = put(d + "/src/lib.cpp", kLibCpp) && ok;
    ok = put(d + "/src/main.cpp", kMainCpp) && ok;
    ok = put(d + "/test/test.cpp", kTestCpp) && ok;
    ok = put(d + "/README.md", kReadme) && ok;
    ok = put(d + "/.clang-format", kFmt) && ok;
    ok = put(d + "/.gitignore", kGit) && ok;
    return ok;
}

} // namespace dcap
