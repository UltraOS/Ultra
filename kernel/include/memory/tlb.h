#pragma once

#include <common/types.h>

void tlb_invalidate_kernel_range(virt_addr_t va_start, virt_addr_t va_end);
