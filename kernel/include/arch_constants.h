#pragma once

#include <common/types.h>
#include <config.h>

#define PAGE_SHIFT CONFIG_PAGE_SHIFT
#define PAGE_SIZE (LITERAL_OF_TYPE(reg_t, 1) << PAGE_SHIFT)

#define CACHE_LINE_SHIFT CONFIG_CACHE_LINE_SHIFT
#define CACHE_LINE_SIZE (LITERAL_OF_TYPE(reg_t, 1) << CACHE_LINE_SHIFT)
