#pragma once

#include <common/types.h>

extern virt_addr_t g_memory_map_base, g_memory_map_end;
#define MEMORY_MAP_BASE g_memory_map_base
#define MEMORY_MAP_END g_memory_map_end

extern virt_addr_t g_valloc_base, g_valloc_end;
#define VALLOC_BASE g_valloc_base
#define VALLOC_END g_valloc_end
