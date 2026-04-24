#pragma once

#include <common/types.h>

// A circular doubly linked list
struct list_link {
    struct list_link *prev;
    struct list_link *next;
};
