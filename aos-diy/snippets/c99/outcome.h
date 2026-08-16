#ifndef AOS_DIY_C99_OUTCOME_H
#define AOS_DIY_C99_OUTCOME_H

#include <stdbool.h>
#include <stddef.h>

typedef enum AosStatus {
  AOS_REJECTED,
  AOS_EXITED,
  AOS_SIGNALLED,
  AOS_LAUNCH_ERROR,
  AOS_UNKNOWN
} AosStatus;

typedef struct AosOutcome {
  AosStatus status;
  int code;
  int signal_number;
  int error_number;
  char reason[256];
  bool timed_out;
  bool output_capped;
} AosOutcome;

AosOutcome aos_outcome_rejected(const char *reason);
AosOutcome aos_outcome_exited(int code);
AosOutcome aos_outcome_signalled(int signal_number);
AosOutcome aos_outcome_launch_error(int error_number, const char *reason);
AosOutcome aos_outcome_unknown(const char *reason);
int aos_exit_number(const AosOutcome *outcome);

#endif
