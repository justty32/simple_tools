#include "outcome.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static AosOutcome make_outcome(AosStatus status, const char *reason) {
  AosOutcome outcome;
  (void)memset(&outcome, 0, sizeof(outcome));
  outcome.status = status;
  if (reason != NULL) {
    (void)snprintf(outcome.reason, sizeof(outcome.reason), "%s", reason);
  }
  return outcome;
}

AosOutcome aos_outcome_rejected(const char *reason) {
  return make_outcome(AOS_REJECTED, reason);
}

AosOutcome aos_outcome_exited(int code) {
  AosOutcome outcome = make_outcome(AOS_EXITED, NULL);
  outcome.code = code;
  return outcome;
}

AosOutcome aos_outcome_signalled(int signal_number) {
  AosOutcome outcome = make_outcome(AOS_SIGNALLED, NULL);
  outcome.signal_number = signal_number;
  return outcome;
}

AosOutcome aos_outcome_launch_error(int error_number, const char *reason) {
  AosOutcome outcome = make_outcome(AOS_LAUNCH_ERROR, reason);
  outcome.error_number = error_number;
  return outcome;
}

AosOutcome aos_outcome_unknown(const char *reason) {
  return make_outcome(AOS_UNKNOWN, reason);
}

int aos_exit_number(const AosOutcome *outcome) {
  if (outcome == NULL) {
    return -1;
  }
  if (outcome->timed_out) {
    return 124;
  }
  if (outcome->output_capped) {
    return 123;
  }
  switch (outcome->status) {
  case AOS_REJECTED:
    return 125;
  case AOS_EXITED:
    return outcome->code;
  case AOS_SIGNALLED:
    return 128 + outcome->signal_number;
  case AOS_LAUNCH_ERROR:
    return outcome->error_number == ENOENT || outcome->error_number == ENOTDIR
               ? 127
               : 126;
  case AOS_UNKNOWN:
    return -1;
  }
  return -1;
}
