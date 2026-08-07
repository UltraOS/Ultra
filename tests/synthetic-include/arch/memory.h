#pragma once

#include <common/types.h>

/*
 * Usermode test stand-in for the kernel's arch/memory.h. The actual values are
 * defined by the test backend (see test_allocators.c) right before valloc_setup,
 * so a test can pick whatever arena/window layout it likes.
 */

extern phys_addr_t g_memory_map_base, g_memory_map_end;
#define MEMORY_MAP_BASE g_memory_map_base
#define MEMORY_MAP_END g_memory_map_end

extern phys_addr_t g_valloc_base, g_valloc_end;
#define VALLOC_BASE g_valloc_base
#define VALLOC_END g_valloc_end
