#pragma once

#include <common/types.h>

#include <sched/wait_queue.h>
#include <detail/sched/completion.h>

/*
 * Something to wait for that happens once, or a counted number of
 * times. A complete() that comes before the wait is remembered, so
 * the two may happen in either order.
 */
struct completion;

#define COMPLETION_INIT(name) (struct completion) { \
    .wq = WAIT_QUEUE_INIT((name).wq),               \
}
#define DEFINE_COMPLETION(name) struct completion name = COMPLETION_INIT(name)

void completion_init(struct completion *completion);

// Let one waiter through. Callable from any context that may take a spinlock
void complete(struct completion *completion);

// Let every waiter through, including future ones
void complete_all(struct completion *completion);

// Sleep until a complete() is called on this object
void wait_for_completion(struct completion *completion);
