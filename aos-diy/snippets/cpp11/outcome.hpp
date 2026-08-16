#pragma once

#include <cerrno>
#include <string>

namespace aos_diy {
namespace snippets11 {

enum class Status { rejected, exited, signalled, launch_error, unknown };

struct Outcome {
  Outcome()
      : status(Status::unknown), code(0), signal(0), error(0), timed_out(false),
        output_capped(false) {}

  static Outcome rejected(const std::string &why) {
    Outcome result;
    result.status = Status::rejected;
    result.reason = why;
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

  static Outcome launch_error(int number, const std::string &why) {
    Outcome result;
    result.status = Status::launch_error;
    result.error = number;
    result.reason = why;
    return result;
  }

  static Outcome unknown(const std::string &why) {
    Outcome result;
    result.reason = why;
    return result;
  }

  Status status;
  int code;
  int signal;
  int error;
  std::string reason;
  bool timed_out;
  bool output_capped;
};

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
    return -1;
  }
  return -1;
}

} // namespace snippets11
} // namespace aos_diy
