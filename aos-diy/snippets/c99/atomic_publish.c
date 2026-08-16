#include "atomic_publish.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *parent_directory(const char *path) {
  const char *slash = strrchr(path, '/');
  char *result;
  size_t length;
  if (slash == NULL) {
    return strdup(".");
  }
  if (slash == path) {
    return strdup("/");
  }
  length = (size_t)(slash - path);
  result = (char *)malloc(length + 1U);
  if (result != NULL) {
    (void)memcpy(result, path, length);
    result[length] = '\0';
  }
  return result;
}

static int write_all(int fd, const unsigned char *contents, size_t size) {
  size_t offset = 0U;
  while (offset < size) {
    ssize_t count = write(fd, contents + offset, size - offset);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return errno;
    }
    if (count == 0) {
      return EIO;
    }
    offset += (size_t)count;
  }
  return 0;
}

int aos_publish_atomically(const char *destination, const void *contents,
                           size_t size) {
  static const char suffix[] = ".tmp.XXXXXX";
  char *directory = NULL;
  char *temporary = NULL;
  int directory_fd = -1;
  int temporary_fd = -1;
  int result = 0;
  int published = 0;
  size_t pattern_size;

  if (destination == NULL || (contents == NULL && size != 0U)) {
    return EINVAL;
  }
  directory = parent_directory(destination);
  if (directory == NULL) {
    return ENOMEM;
  }
  directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory_fd < 0) {
    result = errno;
    goto cleanup;
  }

  pattern_size = strlen(destination) + sizeof(suffix);
  temporary = (char *)malloc(pattern_size);
  if (temporary == NULL) {
    result = ENOMEM;
    goto cleanup;
  }
  (void)snprintf(temporary, pattern_size, "%s%s", destination, suffix);
  temporary_fd = mkstemp(temporary);
  if (temporary_fd < 0) {
    result = errno;
    goto cleanup;
  }
  result = write_all(temporary_fd, (const unsigned char *)contents, size);
  if (result != 0) {
    goto cleanup;
  }
  if (fsync(temporary_fd) < 0) {
    result = errno;
    goto cleanup;
  }
  if (close(temporary_fd) < 0) {
    temporary_fd = -1;
    result = errno;
    goto cleanup;
  }
  temporary_fd = -1;
  if (rename(temporary, destination) < 0) {
    result = errno;
    goto cleanup;
  }
  published = 1;
  if (fsync(directory_fd) < 0) {
    result = errno;
  }

cleanup:
  if (temporary_fd >= 0) {
    (void)close(temporary_fd);
  }
  if (temporary != NULL && !published) {
    (void)unlink(temporary);
  }
  if (directory_fd >= 0) {
    (void)close(directory_fd);
  }
  free(temporary);
  free(directory);
  return result;
}
