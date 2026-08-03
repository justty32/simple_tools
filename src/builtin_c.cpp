#include "builtin.hpp"
#include "util.hpp"

namespace dcap {
namespace {
// unsigned char: template files may contain non-ASCII (UTF-8) bytes, which
// would narrow when stored in a signed char[] braced initializer.
constexpr unsigned char kMakefile[] = {
#embed "../templates/c/Makefile"
    , 0};
constexpr unsigned char kLibH[] = {
#embed "../templates/c/include/lib.h"
    , 0};
constexpr unsigned char kLibC[] = {
#embed "../templates/c/src/lib.c"
    , 0};
constexpr unsigned char kMainC[] = {
#embed "../templates/c/src/main.c"
    , 0};
constexpr unsigned char kTestC[] = {
#embed "../templates/c/test/test.c"
    , 0};
constexpr unsigned char kReadme[] = {
#embed "../templates/c/README.md"
    , 0};
constexpr unsigned char kFmt[] = {
#embed "../templates/c/.clang-format"
    , 0};
constexpr unsigned char kGit[] = {
#embed "../templates/c/.gitignore"
    , 0};

bool put(const std::string& path, const unsigned char* data) {
    return write_file(path, reinterpret_cast<const char*>(data));
}
} // namespace

bool write_builtin_c(const std::string& d) {
    bool ok = ensure_dir(d + "/include") && ensure_dir(d + "/src") &&
              ensure_dir(d + "/test");
    ok = put(d + "/Makefile", kMakefile) && ok;
    ok = put(d + "/include/lib.h", kLibH) && ok;
    ok = put(d + "/src/lib.c", kLibC) && ok;
    ok = put(d + "/src/main.c", kMainC) && ok;
    ok = put(d + "/test/test.c", kTestC) && ok;
    ok = put(d + "/README.md", kReadme) && ok;
    ok = put(d + "/.clang-format", kFmt) && ok;
    ok = put(d + "/.gitignore", kGit) && ok;
    return ok;
}

} // namespace dcap
