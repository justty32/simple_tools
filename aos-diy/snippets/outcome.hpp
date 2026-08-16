#pragma once

#include <cerrno>
#include <string>
#include <utility>

namespace aos_diy::snippets {

enum class Status {
  rejected,
  exited,
  signalled,
  launch_error,
  unknown,
};

struct Outcome {
  Status status = Status::unknown;
  int code = 0;
  int signal = 0;
  int error = 0;
  std::string reason;
  bool timed_out = false;
  bool output_capped = false;

  static Outcome rejected(std::string why) {
    Outcome result;
    result.status = Status::rejected;
    result.reason = std::move(why);
    return result;
  }

  static Outcome exited(int value) {
    Outcome result;
    result.status = Status::exited;
    result.code = value;
    return result;
  }

  static Outcome signalled(int value) {
    Outcome result;
    result.status = Status::signalled;
    result.signal = value;
    return result;
  }

  static Outcome launch_error(int error_number, std::string why) {
    Outcome result;
    result.status = Status::launch_error;
    result.error = error_number;
    result.reason = std::move(why);
    return result;
  }

  static Outcome unknown(std::string why) {
    Outcome result;
    result.status = Status::unknown;
    result.reason = std::move(why);
    return result;
  }
};

// A compact shell-compatible number.  The full Outcome must be retained when
// the distinction between exit(137) and SIGKILL matters.
inline int exit_number(const Outcome &outcome) {
  if (outcome.timed_out) {
    return 124;
  }
  if (outcome.output_capped) {
    return 123;
  }

  switch (outcome.status) {
  case Status::rejected:
    return 125;
  case Status::exited:
    return outcome.code;
  case Status::signalled:
    return 128 + outcome.signal;
  case Status::launch_error:
    return outcome.error == ENOENT || outcome.error == ENOTDIR ? 127 : 126;
  case Status::unknown:
    return -1; // no conclusion: do not publish an exit file
  }
  return -1;
}

} // namespace aos_diy::snippets
