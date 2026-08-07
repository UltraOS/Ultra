#pragma once

#include <common/types.h>
#include <arch/constants.h>

#include <test_harness.h>

// Spelled out rather than via BIT_OF_TYPE to keep the stub free of <bit.h>
#define MAX_PHYS_ADDR (((phys_addr_t)1) << MAX_PHYS_BITS)

// Kernel direct map base, defined by the test backend (test_allocators.c)
extern ptr_t g_direct_map_base;

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
