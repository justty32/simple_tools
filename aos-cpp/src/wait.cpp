#define _POSIX_C_SOURCE 200809L

#include "wait.hpp"

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <limits>

#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aos::detail {
namespace {

constexpr std::uint64_t kMaxPollMs = 50;

bool monotonic_now(timespec &value) {
    return clock_gettime(CLOCK_MONOTONIC, &value) == 0;
}

std::uint64_t elapsed_ms(const timespec &start, const timespec &end) {
    std::uint64_t seconds = static_cast<std::uint64_t>(end.tv_sec - start.tv_sec);
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
        --seconds;
        nanoseconds += 1000000000L;
    }
    if (seconds > std::numeric_limits<std::uint64_t>::max() / 1000) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return seconds * 1000 + static_cast<std::uint64_t>(nanoseconds / 1000000L);
}

void sleep_ms(std::uint64_t milliseconds) {
    timespec request{
        static_cast<time_t>(milliseconds / 1000),
        static_cast<long>((milliseconds % 1000) * 1000000),
    };
    timespec remaining{};
    while (nanosleep(&request, &remaining) < 0 && errno == EINTR) {
        request = remaining;
    }
}

}  // namespace

pid_t wait_retry(pid_t pid, int *status, int options) {
    pid_t waited;
    do {
        waited = waitpid(pid, status, options);
    } while (waited < 0 && errno == EINTR);
    return waited;
}

bool wait_until(pid_t pid, std::uint64_t limit_ms, int &raw_status,
                int &error) {
    timespec start{};
    if (!monotonic_now(start)) {
        error = errno;
        return false;
    }

    std::uint64_t poll_ms = 1;
    for (;;) {
        const pid_t waited = wait_retry(pid, &raw_status, WNOHANG);
        if (waited == pid) {
            return true;
        }
        if (waited < 0) {
            error = errno;
            return false;
        }

        timespec now{};
        if (!monotonic_now(now)) {
            error = errno;
            return false;
        }
        const std::uint64_t elapsed = elapsed_ms(start, now);
        if (elapsed >= limit_ms) {
            return false;
        }
        sleep_ms(poll_ms < limit_ms - elapsed ? poll_ms : limit_ms - elapsed);
        poll_ms = poll_ms < kMaxPollMs / 2 ? poll_ms * 2 : kMaxPollMs;
    }
}

}  // namespace aos::detail
