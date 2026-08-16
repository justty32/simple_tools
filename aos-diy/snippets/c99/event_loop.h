#ifndef AOS_DIY_C99_EVENT_LOOP_H
#define AOS_DIY_C99_EVENT_LOOP_H

#include "blocking_queue.h"
#include "call.h"
#include "outcome.h"

#include <stdbool.h>
#include <stddef.h>

typedef AosOutcome (*AosExecutorFn)(const AosCall *call, void *context);
typedef void (*AosObserverFn)(const AosCall *call, const AosOutcome *outcome,
                              void *context);

typedef struct AosLoopStats {
  size_t handled;
  size_t rejected;
} AosLoopStats;

typedef struct AosEventLoop {
  AosQueue *queue;
  AosExecutorFn executor;
  void *executor_context;
  AosObserverFn observer;
  void *observer_context;
  AosLoopStats stats;
} AosEventLoop;

void aos_loop_init(AosEventLoop *loop, AosQueue *queue, AosExecutorFn executor,
                   void *executor_context);
void aos_loop_set_observer(AosEventLoop *loop, AosObserverFn observer,
                           void *observer_context);
int aos_loop_run(AosEventLoop *loop);
bool aos_loop_step(AosEventLoop *loop);
size_t aos_loop_drain(AosEventLoop *loop);

#endif
