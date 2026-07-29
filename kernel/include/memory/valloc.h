#pragma once

#include <common/types.h>
#include <common/error.h>
#include <common/align.h>
#include <common/bit.h>

#include <memory/alloc.h>
#include <memory/page_table.h>

/*
 * The exclusive end of the address-space arena valloc manages. The topmost
 * page is deliberately left out: an area ending at 2^N wraps back to zero,
 * which every 'start + size' overflow check in the allocator and every
 * 'virt < end' page-table walk would read as a corrupt range. Keeping it out
 * of the arena is what makes those checks exact.
 */
#define KERNEL_VA_END PAGE_ROUND_DOWN(UNSIGNED_MAX(virt_addr_t))

/*
 * Allocate a virtually contiguous region of ram of at least 'size' bytes.
 * Released with vrelease().
 */
void *valloc(size_t size, enum alloc_behavior behavior);

/*
 * Release all resources used by the region at 'ptr'
 * - for valloc'ed regions: unreserves, unmaps & frees the backing memory
 * - for vreserve_and_map'ed regions: unreserves & unmaps
 * - for vreserve'ed regions: unreserves, and unmaps if the region has
 *   been mapped with vmap_reserved()
 */
void vrelease(void *ptr);

/*
 * Reserve a contiguous virtual region of at least 'size' bytes and
 * 'align' bytes aligned that lies within 'start' and 'end' (exclusive).
 * The reservation carries a trailing guard page that is never mapped.
 * Released with vunreserve().
 */
virt_addr_t vreserve_aligned_within(
    virt_addr_t start, virt_addr_t end, size_t align, size_t size
);

static inline virt_addr_t vreserve_within(
    virt_addr_t start, virt_addr_t end, size_t size
)
{
    return vreserve_aligned_within(start, end, 0, size);
}

/*
 * Map a previously reserved area at 'vaddr' to 'paddr'.
 *
 * The area is not released on error, the caller must vunreserve it
 * or retry at their discretion.
 */
void *vmap_reserved(
    virt_addr_t vaddr, phys_addr_t paddr, size_t size,
    pt_prot prot, enum alloc_behavior behavior
);

/*
 * Reserve a virtual region spanning 'size' bytes and map it to a contiguous
 * physical region starting at 'phys' with 'prot'.
 */
void *vreserve_and_map(size_t size, phys_addr_t phys, pt_prot prot);

// vreserve_within() the entire valloc window
virt_addr_t vreserve(size_t size);

/*
 * Unreserve a virtual region at 'addr'
 */
static inline void vunreserve(virt_addr_t addr)
{
    vrelease((void*)addr);
}

/*
 * Mark a generic kernel virtual address space area as reserved
 */
error_t vreserve_permanent(
    virt_addr_t start, virt_addr_t end, const char *what
);
