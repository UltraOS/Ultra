#pragma once

#include <common/types.h>
#include <common/rb_tree.h>

struct timer_queue;
enum timer_flags : u32;
enum timer_result : u32;

struct timer {
    u64 deadline_ns;

    enum timer_result (*fn)(struct timer *timer);

    struct rb_node node;
    struct timer_queue *queue;
    enum timer_flags flags;
};
