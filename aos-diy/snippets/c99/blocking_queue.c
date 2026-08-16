#include "blocking_queue.h"

#include <errno.h>
#include <stdlib.h>

typedef struct AosQueueNode {
  void *value;
  struct AosQueueNode *next;
} AosQueueNode;

int aos_queue_init(AosQueue *queue) {
  int error;
  if (queue == NULL) {
    return EINVAL;
  }

  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0U;
  queue->closed = false;
  error = pthread_mutex_init(&queue->mutex, NULL);
  if (error != 0) {
    return error;
  }
  error = pthread_cond_init(&queue->ready, NULL);
  if (error != 0) {
    (void)pthread_mutex_destroy(&queue->mutex);
    return error;
  }
  return 0;
}

int aos_queue_push(AosQueue *queue, void *value) {
  AosQueueNode *node;
  int error;
  if (queue == NULL) {
    return EINVAL;
  }

  node = (AosQueueNode *)malloc(sizeof(*node));
  if (node == NULL) {
    return ENOMEM;
  }
  node->value = value;
  node->next = NULL;

  error = pthread_mutex_lock(&queue->mutex);
  if (error != 0) {
    free(node);
    return error;
  }
  if (queue->closed) {
    (void)pthread_mutex_unlock(&queue->mutex);
    free(node);
    return ECANCELED;
  }
  if (queue->tail != NULL) {
    queue->tail->next = node;
  } else {
    queue->head = node;
  }
  queue->tail = node;
  ++queue->size;
  error = pthread_mutex_unlock(&queue->mutex);
  if (error != 0) {
    return error;
  }
  return pthread_cond_signal(&queue->ready);
}

static AosTakeResult pop_locked(AosQueue *queue, void **value) {
  AosQueueNode *node = queue->head;
  if (node == NULL) {
    return queue->closed ? AOS_TAKE_CLOSED : AOS_TAKE_EMPTY;
  }

  queue->head = node->next;
  if (queue->head == NULL) {
    queue->tail = NULL;
  }
  --queue->size;
  *value = node->value;
  free(node);
  return AOS_TAKE_ITEM;
}

AosTakeResult aos_queue_take(AosQueue *queue, void **value) {
  AosTakeResult result;
  int error;
  if (queue == NULL || value == NULL) {
    return AOS_TAKE_ERROR;
  }
  error = pthread_mutex_lock(&queue->mutex);
  if (error != 0) {
    return AOS_TAKE_ERROR;
  }
  while (queue->head == NULL && !queue->closed) {
    error = pthread_cond_wait(&queue->ready, &queue->mutex);
    if (error != 0) {
      (void)pthread_mutex_unlock(&queue->mutex);
      return AOS_TAKE_ERROR;
    }
  }
  result = pop_locked(queue, value);
  if (pthread_mutex_unlock(&queue->mutex) != 0) {
    return AOS_TAKE_ERROR;
  }
  return result;
}

AosTakeResult aos_queue_try_take(AosQueue *queue, void **value) {
  AosTakeResult result;
  if (queue == NULL || value == NULL) {
    return AOS_TAKE_ERROR;
  }
  if (pthread_mutex_lock(&queue->mutex) != 0) {
    return AOS_TAKE_ERROR;
  }
  result = pop_locked(queue, value);
  if (pthread_mutex_unlock(&queue->mutex) != 0) {
    return AOS_TAKE_ERROR;
  }
  return result;
}

int aos_queue_close(AosQueue *queue) {
  int error;
  if (queue == NULL) {
    return EINVAL;
  }
  error = pthread_mutex_lock(&queue->mutex);
  if (error != 0) {
    return error;
  }
  queue->closed = true;
  error = pthread_mutex_unlock(&queue->mutex);
  if (error != 0) {
    return error;
  }
  return pthread_cond_broadcast(&queue->ready);
}

size_t aos_queue_size(AosQueue *queue) {
  size_t result = 0U;
  if (queue != NULL && pthread_mutex_lock(&queue->mutex) == 0) {
    result = queue->size;
    (void)pthread_mutex_unlock(&queue->mutex);
  }
  return result;
}

bool aos_queue_closed(AosQueue *queue) {
  bool result = false;
  if (queue != NULL && pthread_mutex_lock(&queue->mutex) == 0) {
    result = queue->closed;
    (void)pthread_mutex_unlock(&queue->mutex);
  }
  return result;
}

int aos_queue_destroy(AosQueue *queue) {
  AosQueueNode *node;
  int first_error = 0;
  int error;
  if (queue == NULL) {
    return EINVAL;
  }
  node = queue->head;
  while (node != NULL) {
    AosQueueNode *next = node->next;
    free(node);
    node = next;
  }
  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0U;

  error = pthread_cond_destroy(&queue->ready);
  if (error != 0) {
    first_error = error;
  }
  error = pthread_mutex_destroy(&queue->mutex);
  if (first_error == 0 && error != 0) {
    first_error = error;
  }
  return first_error;
}
