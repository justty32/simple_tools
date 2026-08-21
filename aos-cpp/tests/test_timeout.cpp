#define _POSIX_C_SOURCE 200809L

#include <aos/exec.hpp>
#include <aos/format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <dirent.h>
#include <time.h>
#include <unistd.h>

namespace {

std::string make_temp_dir() {
    std::string pattern = "aos_timeout_test_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    REQUIRE(mkdtemp(buffer.data()) != nullptr);
    return buffer.data();
}

void remove_temp_dir(const std::string &path) {
    DIR *dir = opendir(path.c_str());
    if (dir != nullptr) {
        while (dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name != "." && name != "..") {
                std::remove((path + "/" + name).c_str());
            }
        }
        closedir(dir);
    }
    rmdir(path.c_str());
}

struct TempDir {
    TempDir() : path(make_temp_dir()) {}
    ~TempDir() { remove_temp_dir(path); }
    std::string path;
};

aos::inst_t shell_command(const std::string &script) {
    aos::inst_t inst;
    inst.argv = {"/bin/sh", "-c", script};
    return inst;
}

void sleep_for_ms(long milliseconds) {
    timespec request{milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    timespec remaining{};
    while (nanosleep(&request, &remaining) < 0 && errno == EINTR) {
        request = remaining;
    }
}

}  // namespace

TEST_CASE("zero timeout uses normal blocking execution") {
    aos::inst_t inst = shell_command("sleep 1");
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.status == 0);
    CHECK_FALSE(result.timed_out);
}

TEST_CASE("timeout terminates a command process group") {
    aos::inst_t inst = shell_command("sleep 300");
    inst.timeout_ms = 100;
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.timed_out);
    CHECK((result.status == 143 || result.status == 137));
}

TEST_CASE("fast command does not pay a polling interval") {
    aos::inst_t inst = shell_command("exit 0");
    inst.timeout_ms = 300;
    aos::ExecResult result;
    const auto start = std::chrono::steady_clock::now();

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    const auto duration = std::chrono::steady_clock::now() - start;
    CHECK_FALSE(result.timed_out);
    CHECK(duration < std::chrono::milliseconds(50));
}

TEST_CASE("timeout kills grandchildren") {
    TempDir dir;
    const std::string marker = dir.path + "/survived";
    aos::inst_t inst = shell_command(
        "(sleep 1; touch " + marker + ") & sleep 300");
    inst.timeout_ms = 100;
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    REQUIRE(result.timed_out);
    sleep_for_ms(1100);
    CHECK(access(marker.c_str(), F_OK) != 0);
}

TEST_CASE("timeout format reads writes and rejects invalid values") {
    aos::inst_t inst;
    const std::string valid =
        R"({"argv":["sleep","1"],"timeout_ms":250})";
    REQUIRE(aos::read_one(valid.data(), valid.size(), inst) ==
            aos::InstState::Ok);
    CHECK(inst.timeout_ms == 250);

    std::string encoded;
    REQUIRE(aos::write_one(inst, encoded) == aos::InstState::Ok);
    aos::inst_t decoded;
    REQUIRE(aos::read_one(encoded.data(), encoded.size() - 1, decoded) ==
            aos::InstState::Ok);
    CHECK(decoded.timeout_ms == 250);

    const std::string negative = R"({"argv":["x"],"timeout_ms":-1})";
    CHECK(aos::read_one(negative.data(), negative.size(), decoded) ==
          aos::InstState::FieldTypeMismatch);
    const std::string fractional = R"({"argv":["x"],"timeout_ms":1.5})";
    CHECK(aos::read_one(fractional.data(), fractional.size(), decoded) ==
          aos::InstState::FieldTypeMismatch);
    const std::string wrong_type = R"({"argv":["x"],"timeout_ms":"100"})";
    CHECK(aos::read_one(wrong_type.data(), wrong_type.size(), decoded) ==
          aos::InstState::FieldTypeMismatch);

    inst.timeout_ms = 0;
    encoded.clear();
    REQUIRE(aos::write_one(inst, encoded) == aos::InstState::Ok);
    CHECK(encoded.find("timeout_ms") == std::string::npos);
}
