#pragma once

#include <locking/spinlock.h>

struct mutex {
    struct spinlock lock;
};
