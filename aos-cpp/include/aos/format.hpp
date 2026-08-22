#pragma once

#include <aos/export.h>
#include <aos/inst.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace aos {

AOS_API InstState read_all(const char *data, std::size_t size,
                           std::vector<inst_t> &out,
                           std::size_t *error_record);

AOS_API InstState read_one(const char *data, std::size_t size,
                           inst_t &out);

AOS_API InstState write_one(const inst_t &inst, std::string &out);

AOS_API InstState write_all(const std::vector<inst_t> &insts, std::string &out,
                            std::size_t *error_record);

}  // namespace aos
