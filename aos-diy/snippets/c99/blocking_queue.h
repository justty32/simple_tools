#ifndef AOS_DIY_C99_BLOCKING_QUEUE_H
#define AOS_DIY_C99_BLOCKING_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

struct AosQueueNode;

typedef struct AosQueue {
  pthread_mutex_t mutex;
  pthread_cond_t ready;
  struct AosQueueNode *head;
  struct AosQueueNode *tail;
  size_t size;
  bool closed;
} AosQueue;

typedef enum AosTakeResult {
  AOS_TAKE_ERROR = -1,
  AOS_TAKE_CLOSED = 0,
  AOS_TAKE_ITEM = 1,
  AOS_TAKE_EMPTY = 2
} AosTakeResult;

/* Returns 0, ENOMEM, ECANCELED (already closed), or a pthread error code. */
int aos_queue_init(AosQueue *queue);
int aos_queue_push(AosQueue *queue, void *value);
AosTakeResult aos_queue_take(AosQueue *queue, void **value);
AosTakeResult aos_queue_try_take(AosQueue *queue, void **value);
int aos_queue_close(AosQueue *queue);
size_t aos_queue_size(AosQueue *queue);
bool aos_queue_closed(AosQueue *queue);

/* The caller must first stop all users. Queued values are not freed. */
int aos_queue_destroy(AosQueue *queue);

#endif
