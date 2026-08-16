#pragma once

#include "call.hpp"
#include "outcome.hpp"

namespace aos_diy::snippets {

// argv[0] must be an already resolved executable path. cwd and env are taken
// directly from Call. env names a shell file to source over a clean base.
Outcome run_process(const Call &call);

} // namespace aos_diy::snippets
