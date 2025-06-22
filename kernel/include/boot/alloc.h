#pragma once

#include <common/error.h>

void boot_alloc_init(void);

phys_addr_or_error_t boot_alloc(size_t num_pages);
phys_addr_or_error_t boot_alloc_at(phys_addr_t addr, size_t num_pages);

void boot_free(phys_addr_t address, size_t num_pages);
