#include "../src/run.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <unistd.h>

namespace {

std::string read_file(const std::string &path) {
    std::ifstream input(path);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void write_file(const std::string &path, const std::string &content) {
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    output << content;
}

std::string make_temp_dir() {
    std::string pattern = "aos_run_test_XXXXXX";
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

int run_file(std::string &path) {
    char program[] = "aos-cpp";
    char *argv[] = {program, path.data()};
    return aos::run(2, argv);
}

}  // namespace

TEST_CASE("run validates the whole file before executing any record") {
    TempDir dir;
    const std::string marker = dir.path + "/marker";
    std::string input = dir.path + "/input.jsonl";
    write_file(input,
               "{\"argv\":[\"/bin/sh\",\"-c\",\"printf ran > " + marker +
                   "\"]}\n"
                   "{\"argv\":[]}\n");

    CHECK(run_file(input) == 1);
    CHECK(access(marker.c_str(), F_OK) != 0);
}

TEST_CASE("run executes multiple records sequentially") {
    TempDir dir;
    const std::string output = dir.path + "/order";
    std::string input = dir.path + "/input.jsonl";
    write_file(input,
               "{\"argv\":[\"/bin/sh\",\"-c\",\"printf first > " + output +
                   "\"]}\n"
                   "{\"argv\":[\"/bin/sh\",\"-c\",\"exit 7\"]}\n"
                   "{\"argv\":[\"/bin/sh\",\"-c\",\"test $(cat " + output +
                   ") = first && printf second >> " + output +
                   "\"]}\n");

    CHECK(run_file(input) == 0);
    CHECK(read_file(output) == "firstsecond");
}
