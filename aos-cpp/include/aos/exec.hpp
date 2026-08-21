#pragma once

#include <aos/export.h>
#include <aos/inst.hpp>

namespace aos {

enum class ExecState {
    Ok,
    InvalidArgument,
    SpawnFailed,
    WaitFailed,
    ExitWriteFailed,
};

struct ExecResult {
    int status = 0;
    bool signalled = false;
    bool timed_out = false;
    int error = 0;
};

AOS_API ExecState execute(inst_t &inst, ExecResult &result);
AOS_API const char *to_string(ExecState state) noexcept;

}  // namespace aos
