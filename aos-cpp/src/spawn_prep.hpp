#pragma once

#include <aos/inst.hpp>

#include <string>
#include <vector>

namespace aos::detail {

struct SpawnPrep {
    std::vector<std::string> environment;
    std::vector<char *> envp;
    std::string executable;
    int failure_status = 0;
};

void prepare_spawn(const inst_t &inst, SpawnPrep &prep);

}  // namespace aos::detail
