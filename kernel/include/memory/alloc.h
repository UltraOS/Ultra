#pragma once

#include <common/types.h>

#include <memory/alloc_behavior.h>

void *alloc(size_t size, enum alloc_behavior);
void free(void*);
