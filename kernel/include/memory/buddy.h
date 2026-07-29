#pragma once

#include <common/types.h>
#include <common/error.h>
#include <common/helpers.h>
#include <common/bit.h>

#include <arch/constants.h>

#include <memory/alloc_behavior.h>
#include <memory/page.h>

#define BUDDY_MAX_ORDER 10

static inline size_t order_to_bytes(u8 order)
{
    return PAGE_SIZE * BIT_PHYS(order);
}

/*
 * Allocate a generic reference counted physically contiguous block of size
 * 1 << (PAGE_SHIFT + order) bytes. Release via block_unref().
 */
struct page_block *alloc_block(u8 order, enum alloc_behavior);

// Shorthand for order 0 block allocations
static inline struct page_block *alloc_page(enum alloc_behavior behavior)
{
    return alloc_block(0, behavior);
}

/*
 * Allocate 'count' order 0 generic reference counted blocks by filling the
 * NULL slots of the caller-provided 'blocks' array. Returns the total number
 * of populated slots in the array after this call. May make partial progress,
 * call in a loop while at least one new slot is populated per call.
 */
size_t alloc_blocks_bulk(
    size_t count, struct page_block **blocks, enum alloc_behavior
);

/*
 * Drop one reference on every non-NULL entry in the first 'count'
 * slots of 'blocks' and set those slots back to NULL. Safe for
 * partially populated arrays.
 */
void free_blocks_bulk(struct page_block **blocks, size_t count);

/*
 * All-or-nothing wrapper around alloc_blocks_bulk(). 'blocks' must
 * contain only NULL entries on entry: on failure every block this call
 * allocated is released, which is only distinguishable from caller
 * entries if the array started empty.
 */
error_t alloc_blocks_bulk_or_fail(
    size_t count, struct page_block **blocks, enum alloc_behavior
);

/*
 * Same as alloc_block(), except the block does not participate in
 * reference counting. Must be released via free_frozen_block().
 * For callers that track lifetime structurally and don't want to pay
 * per-page atomics.
 */
struct page_block *alloc_frozen_block(u8 order, enum alloc_behavior);
void free_frozen_block(struct page_block *block);

/*
 * Allocate a page to be used for one of the intermediate page table
 * levels. The allocated page is a PAGE_TYPE_PTDESC.
 */
struct page_block *alloc_page_table(enum alloc_behavior behavior);
