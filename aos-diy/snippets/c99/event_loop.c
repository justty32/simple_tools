#include "event_loop.h"

#include <errno.h>
#include <string.h>

void aos_loop_init(AosEventLoop *loop, AosQueue *queue, AosExecutorFn executor,
                   void *executor_context) {
  (void)memset(loop, 0, sizeof(*loop));
  loop->queue = queue;
  loop->executor = executor;
  loop->executor_context = executor_context;
}

void aos_loop_set_observer(AosEventLoop *loop, AosObserverFn observer,
                           void *observer_context) {
  loop->observer = observer;
  loop->observer_context = observer_context;
}

static void handle(AosEventLoop *loop, const AosCall *call) {
  AosOutcome outcome;
  ++loop->stats.handled;
  outcome = loop->executor(call, loop->executor_context);
  if (outcome.status == AOS_REJECTED) {
    ++loop->stats.rejected;
  }
  if (loop->observer != NULL) {
    loop->observer(call, &outcome, loop->observer_context);
  }
}

int aos_loop_run(AosEventLoop *loop) {
  for (;;) {
    void *value = NULL;
    AosTakeResult result = aos_queue_take(loop->queue, &value);
    if (result == AOS_TAKE_CLOSED) {
      return 0;
    }
    if (result != AOS_TAKE_ITEM) {
      return EIO;
    }
    handle(loop, (const AosCall *)value);
  }
}

bool aos_loop_step(AosEventLoop *loop) {
  void *value = NULL;
  AosTakeResult result = aos_queue_try_take(loop->queue, &value);
  if (result != AOS_TAKE_ITEM) {
    return false;
  }
  handle(loop, (const AosCall *)value);
  return true;
}

size_t aos_loop_drain(AosEventLoop *loop) {
  size_t count = 0U;
  while (aos_loop_step(loop)) {
    ++count;
  }
  return count;
}
