#ifndef AOS_DIY_C99_CALL_H
#define AOS_DIY_C99_CALL_H

#include <stddef.h>

/* Borrowed strings: the producer owns them until the consumer is finished. */
typedef struct AosCall {
  size_t argc;
  const char *const *argv;
  const char *stdin_path;
  const char *stdout_path;
  const char *stderr_path;
  const char *exit_path;
  const char *cwd;
  const char *env;
  const char *user;
} AosCall;

#endif
