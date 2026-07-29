#pragma once

#include <common/types.h>
#include <common/list.h>

#include <memory/alloc.h>

#include <spinlock.h>

struct alloc_cache {
    const char *name;
    size_t object_size;
    u8 order;

    struct list_link partial_slabs;
    struct list_link full_slabs;
    struct list_link empty_slabs;

    size_t num_empty_slabs;
    size_t max_empty_slabs;

    struct spinlock lock;
};

/*
 * Initialize a dedicated cache of 'size'-byte objects, each aligned to
 * 'align' bytes (a power of two up to PAGE_SIZE, zero for the default).
 * Never allocates, the backing memory is provisioned lazily.
 *
 * Objects are handed back via the regular free().
 */
void alloc_cache_init(
    struct alloc_cache *cache, const char *name, size_t size, size_t align
);

void *alloc_from_cache(struct alloc_cache*, enum alloc_behavior);

/*
 * Release every retained slab back to the buddy allocator. All objects
 * must have been freed by this point, and the caller is responsible for
 * making sure the cache can no longer be reached by anyone else.
 */
void alloc_cache_destroy(struct alloc_cache*);
