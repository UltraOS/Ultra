#pragma once

#ifndef __ASSEMBLER__
#include <common/types.h>

extern u8 g_max_phys_bits;
#define MAX_PHYS_BITS g_max_phys_bits
#endif

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define CACHE_LINE_SIZE 64
