#pragma once

#include <common/error.h>

void kernel_memory_map_setup(void);
void buddy_setup(void);
void kernel_heap_init(void);
void valloc_setup(void);
