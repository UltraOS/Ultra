#pragma once

#include <common/types.h>
#include <arch/constants.h>

#include <test_harness.h>

static inline phys_addr_t virt_to_phys(void *virt)
{
    return translate_virt_to_phys(virt);
}

static inline void *phys_to_virt(phys_addr_t phys)
{
    return translate_phys_to_virt(phys);
}

static inline void *io_window_map_cached(phys_addr_t phys_base, size_t length)
{
    UNREFERENCED_PARAMETER(length);
    return phys_to_virt(phys_base);
}

static inline void io_window_unmap_ptr(void *ptr, size_t length)
{
    UNREFERENCED_PARAMETER(ptr);
    UNREFERENCED_PARAMETER(length);
}
