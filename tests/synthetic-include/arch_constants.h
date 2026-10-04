#pragma once

#include <common/types.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE (LITERAL_OF_TYPE(reg_t, 1) << PAGE_SHIFT)
