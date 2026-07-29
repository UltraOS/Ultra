#pragma once

#include <memory/buddy.h>

/*
 * Byte size of a block of the highest order. This is a contract
 * between the memmap builder and the buddy: the memmap is populated
 * around RAM ranges in aligned stretches of this size, and buddy
 * merge probing never crosses a boundary of this alignment.
 */
#define BUDDY_MAX_SIZE BIT_PHYS(PAGE_SHIFT + BUDDY_MAX_ORDER)

struct page_block *alloc_typed_block(
    u8 order, enum alloc_behavior, enum page_type type
);

size_t alloc_typed_blocks_bulk(
    size_t count, struct page_block **blocks, enum alloc_behavior,
    enum page_type type
);

void free_frozen_blocks_bulk(struct page_block **blocks, size_t count);

static inline error_t alloc_typed_blocks_bulk_or_fail(
    size_t count, struct page_block **blocks, enum alloc_behavior behavior,
    enum page_type type
)
{
    size_t num_populated = 0, this_batch;

    while (num_populated < count) {
        this_batch = alloc_typed_blocks_bulk(count, blocks, behavior, type);
        if (unlikely(this_batch == num_populated))
            goto out_fail;

        num_populated = this_batch;
    }

    return EOK;

out_fail:
    // The whole array is one class, free it via the matching path
    if (page_type_is_refcounted(type))
        free_blocks_bulk(blocks, count);
    else
        free_frozen_blocks_bulk(blocks, count);
    return ENOMEM;
}
