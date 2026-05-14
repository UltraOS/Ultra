#pragma once

#include <common/types.h>
#include <test_harness.h>

static inline phys_addr_t virt_to_phys(void *virt)
{
    return translate_virt_to_phys(virt);
}

static inline void *phys_to_virt(phys_addr_t phys)
{
    return translate_phys_to_virt(phys);
}
