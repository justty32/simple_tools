#pragma once

#include <aos/export.h>
#include <aos/inst.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace aos {

struct ReadOptions {
    std::size_t max_record_bytes = 1u << 20;
    std::size_t max_total_bytes  = 64u << 20;
};

AOS_API InstState read_all(const char *data, std::size_t size,
                           std::vector<inst_t> &out,
                           std::size_t *error_line,
                           const ReadOptions &opts = {});

AOS_API InstState read_one(const char *line, std::size_t size,
                           inst_t &out, const ReadOptions &opts = {});

AOS_API InstState write_one(const inst_t &inst, std::string &out);

}  // namespace aos
