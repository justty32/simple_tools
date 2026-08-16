#ifndef AOS_DIY_C99_POSIX_PROCESS_H
#define AOS_DIY_C99_POSIX_PROCESS_H

#include "call.h"
#include "outcome.h"

/* argv[0] must be resolved. cwd and the env-file path come from AosCall. */
AosOutcome aos_run_process(const AosCall *call);

#endif
