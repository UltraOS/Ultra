#pragma once

#include <common/types.h>
#include <common/list.h>

#include <locking/spinlock.h>

struct mutex {
    ptr_t owner;
    struct spinlock wait_lock;
    struct list_link waiters;
};
