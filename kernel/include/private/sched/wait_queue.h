#pragma once

#include <common/types.h>

struct wait_queue;
struct wait_entry;

void wait_queue_prepare_unlocked(
    struct wait_queue *wq, struct wait_entry *entry
);
void wait_queue_remove_unlocked(
    struct wait_queue *wq, struct wait_entry *entry
);

/*
 * Wakes every plain waiter and 'num_exclusive' exclusive ones, all of
 * them if zero.
 */
void wait_queue_wake_unlocked(struct wait_queue *wq, u32 num_exclusive);
