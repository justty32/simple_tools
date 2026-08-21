#pragma once

#include <cstdint>

#include <sys/types.h>

namespace aos::detail {

pid_t wait_retry(pid_t pid, int *status, int options);
bool wait_until(pid_t pid, std::uint64_t limit_ms, int &raw_status, int &error);

}  // namespace aos::detail
