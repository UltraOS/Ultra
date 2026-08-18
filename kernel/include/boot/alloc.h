#pragma once

#include <common/error.h>
#include <boot/boot.h>

phys_addr_or_error_t boot_alloc(size_t num_pages);
phys_addr_or_error_t boot_alloc_zeroed(size_t num_pages);

phys_addr_or_error_t boot_alloc_aligned(size_t num_pages, size_t align);
phys_addr_or_error_t boot_alloc_aligned_zeroed(size_t num_pages, size_t align);

void *boot_alloc_or_die(size_t num_pages, const char *why);
void *boot_alloc_zeroed_or_die(size_t num_pages, const char *why);

phys_addr_or_error_t boot_alloc_at(phys_addr_t addr, size_t num_pages);

void boot_free(phys_addr_t address, size_t num_pages);

struct boot_alloc_for_each_ctx {
    bool is_free;
};
void boot_alloc_for_each_range(memory_range_cb_t cb);
