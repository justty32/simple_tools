#define _POSIX_C_SOURCE 200809L

#include "run.hpp"

#include <aos/exec.hpp>
#include <aos/format.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace aos {
namespace {

int open_input(const char *path) {
    int fd;
    do {
        fd = open(path, O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

bool read_input(int fd, std::size_t limit, std::string &buffer, int &error,
                bool &too_long) {
    char chunk[64 * 1024];
    for (;;) {
        ssize_t count;
        do {
            count = read(fd, chunk, sizeof(chunk));
        } while (count < 0 && errno == EINTR);

        if (count < 0) {
            error = errno;
            return false;
        }
        if (count == 0) {
            return true;
        }

        const auto size = static_cast<std::size_t>(count);
        if (size > limit - buffer.size()) {
            too_long = true;
            return false;
        }
        buffer.append(chunk, size);
    }
}

}  // namespace

int run(int argc, char *argv[]) {
    if (argc < 1 || argc > 2) {
        const char *program = argc > 0 && argv != nullptr && argv[0] != nullptr
                                  ? argv[0]
                                  : "aos-cpp";
        std::fprintf(stderr, "usage: %s [file]\n", program);
        return 2;
    }

    const bool from_file = argc == 2;
    const char *source = from_file ? argv[1] : "standard input";
    int fd = STDIN_FILENO;
    if (from_file) {
        fd = open_input(source);
        if (fd < 0) {
            std::fprintf(stderr, "aos-cpp: cannot open %s: %s\n", source,
                         std::strerror(errno));
            return 1;
        }
    }

    const ReadOptions options;
    std::string buffer;
    int read_error = 0;
    bool too_long = false;
    const bool read_ok =
        read_input(fd, options.max_total_bytes, buffer, read_error, too_long);

    int close_error = 0;
    if (from_file && close(fd) != 0) {
        close_error = errno;
    }
    if (!read_ok) {
        if (too_long) {
            std::fprintf(stderr, "aos-cpp: %s exceeds the input size limit\n",
                         source);
        } else {
            std::fprintf(stderr, "aos-cpp: cannot read %s: %s\n", source,
                         std::strerror(read_error));
        }
        return 1;
    }
    if (close_error != 0) {
        std::fprintf(stderr, "aos-cpp: cannot close %s: %s\n", source,
                     std::strerror(close_error));
        return 1;
    }

    std::vector<inst_t> instructions;
    std::size_t error_line = 0;
    const char empty = '\0';
    const char *data = buffer.empty() ? &empty : buffer.data();
    const InstState parse_state =
        read_all(data, buffer.size(), instructions, &error_line, options);
    if (parse_state != InstState::Ok) {
        std::fprintf(stderr, "aos-cpp: %s:%zu: %s\n", source, error_line,
                     to_string(parse_state));
        return 1;
    }

    bool failed = false;
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        ExecResult result;
        const ExecState state = execute(instructions[index], result);
        if (state != ExecState::Ok) {
            failed = true;
            if (result.error != 0) {
                std::fprintf(stderr, "aos-cpp: record %zu: %s: %s\n",
                             index + 1, to_string(state),
                             std::strerror(result.error));
            } else {
                std::fprintf(stderr, "aos-cpp: record %zu: %s\n", index + 1,
                             to_string(state));
            }
        }
    }
    return failed ? 1 : 0;
}

}  // namespace aos
