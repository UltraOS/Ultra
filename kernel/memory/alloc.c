#define MSG_FMT(msg) "alloc: " msg

#include <common/types.h>
#include <common/bit.h>
#include <common/string.h>
#include <common/align.h>
#include <common/minmax.h>
#include <common/slist.h>

#include <memory/alloc.h>
#include <memory/alloc_cache.h>
#include <memory/buddy.h>
#include <memory/page.h>
#include <memory/units.h>
#include <private/buddy.h>

#include <bug.h>
#include <spinlock.h>
#include <free_after_init.h>
#include <init_level.h>
#include <log.h>

/*
 * We allow this many KiB to stay in the cache before handing the
 * memory back over to the buddy allocator
 */
#define TARGET_RETAINED_BYTES (64 * KiB)

// The largest slab we're willing to provision per-cache
#define MAX_SLAB_ORDER 3

#define NUM_BUILTIN_CACHES 11
#define MIN_CACHE_SHIFT 3
#define MIN_CACHE_SIZE 8

struct alloc_cache s_builtin_caches[NUM_BUILTIN_CACHES];

static const char *const INIT_RODATA
s_builtin_cache_names[NUM_BUILTIN_CACHES] = {
    "builtin_8", "builtin_16", "builtin_32", "builtin_64",
    "builtin_128", "builtin_256", "builtin_512", "builtin_1k",
    "builtin_2k", "builtin_4k", "builtin_8k"
};

static inline size_t size_to_cache_index(size_t size)
{
    size_t bit;

    if (size <= MIN_CACHE_SIZE)
        return 0;

    bit = 64 - __builtin_clzll(size - 1);
    return bit - MIN_CACHE_SHIFT;
}

static u8 size_to_order(size_t size, size_t *out_allocation_size)
{
    size_t allocation_size;
    u8 order = 0;

    allocation_size = order_to_bytes(0);

    while (allocation_size < size) {
        order++;
        allocation_size = order_to_bytes(order);
    }

    if (out_allocation_size)
        *out_allocation_size = allocation_size;

    return order;
}

/*
 * Pick the smallest slab order that keeps the tail waste (the cut-off
 * remainder past the last object) at a small fraction of the slab,
 * starting at 1/16 and progressively accepting more if nothing under
 * MAX_SLAB_ORDER can deliver it.
 */
static u8 cache_optimal_order(size_t object_size)
{
    size_t fraction, slab_size;
    u8 order, min_order;

    min_order = size_to_order(object_size, nullptr);

    for (fraction = 16; fraction > 1; fraction /= 2) {
        for (order = min_order; order <= MAX_SLAB_ORDER; order++) {
            slab_size = order_to_bytes(order);

            if ((slab_size % object_size) <= slab_size / fraction)
                return order;
        }
    }

    return min_order;
}

void alloc_cache_init(
    struct alloc_cache *cache, const char *name, size_t size, size_t align
)
{
    size_t bytes_per_allocation;

    BUG_ON(size == 0);
    BUG_ON(size > BUDDY_MAX_SIZE);

    if (align == 0)
        align = MIN_CACHE_SIZE;

    BUG_ON(!IS_POWER_OF_TWO(align));
    BUG_ON(align > PAGE_SIZE);
    align = MAX(align, (size_t)MIN_CACHE_SIZE);

    cache->name = name;

    /*
     * Slabs come from the buddy allocator naturally aligned to their
     * size, so keeping the stride a multiple of 'align' makes every
     * object 'align'-aligned.
     */
    cache->object_size = ALIGN_UP(
        MAX(size, sizeof(struct slist_node)), align
    );
    cache->num_empty_slabs = 0;
    cache->order = cache_optimal_order(cache->object_size);

    bytes_per_allocation = order_to_bytes(cache->order);
    cache->max_empty_slabs = TARGET_RETAINED_BYTES / bytes_per_allocation;
    if (cache->max_empty_slabs == 0)
        cache->max_empty_slabs = 1;

    list_init(&cache->partial_slabs);
    list_init(&cache->full_slabs);
    list_init(&cache->empty_slabs);
    spin_lock_init(&cache->lock);
}

static error_t INIT_CODE kernel_heap_init(void)
{
    struct alloc_cache *cache;
    size_t i, size = MIN_CACHE_SIZE;

    for (i = 0; i < NUM_BUILTIN_CACHES; i++) {
        alloc_cache_init(
            &s_builtin_caches[i], s_builtin_cache_names[i], size, 0
        );
        size *= 2;
    }

    pr_info("now online, builtin caches listed below\n");
    pr_info("  %-12s %-12s %-8s %-12s\n",
            "Name", "Object Size", "Order", "Max Retained");

    for (i = 0; i < NUM_BUILTIN_CACHES; i++) {
        cache = &s_builtin_caches[i];

        pr_info(
            "  %-12s %-12zu %-8u %-12zu\n",
            cache->name, cache->object_size, cache->order,
            cache->max_empty_slabs
        );
    }

    return EOK;
}
INIT_CALL_AT(HEAP_AVAILABLE, kernel_heap_init);

static struct kheap_page *slab_create(
    struct alloc_cache *cache, enum alloc_behavior behavior
)
{
    struct page_block *block;
    struct kheap_page *heap;
    struct slist_node *node;
    size_t num_objects, i;
    void *cursor;

    block = alloc_typed_block(cache->order, behavior, PAGE_TYPE_KHEAP);
    if (block == nullptr)
        return nullptr;

    heap = block_kheap(block);

    heap->cache = cache;
    heap->num_allocated_objects = 0;
    slist_init(&heap->freelist);

    cursor = block_to_virt(block);
    num_objects = order_to_bytes(cache->order) / cache->object_size;

    for (i = 0; i < num_objects; i++) {
        node = cursor;
        slist_push(&heap->freelist, node);
        cursor += cache->object_size;
    }

    return heap;
}

static void *cache_take_object(
    struct alloc_cache *cache, enum alloc_behavior behavior
)
{
    struct kheap_page *heap;
    struct slist_node *node;
    irq_state_t state;

    state = spin_lock_irq_save(&cache->lock);

    if (!list_is_empty(&cache->partial_slabs)) {
        heap = list_first_entry(
            &cache->partial_slabs, struct kheap_page, link
        );
    } else if (!list_is_empty(&cache->empty_slabs)) {
        heap = list_first_entry(
            &cache->empty_slabs, struct kheap_page, link
        );
        list_remove(&heap->link);
        list_insert_next(&cache->partial_slabs, &heap->link);
        cache->num_empty_slabs--;
    } else {
        spin_unlock_irq_restore(&cache->lock, state);

        /*
         * Provision the slab with the lock dropped: the buddy may
         * eventually sleep or reclaim for ALLOC_GENERIC, and the
         * freelist threading is O(num_objects). Racing allocators
         * may publish a slab of their own meanwhile, an extra
         * partial one is harmless.
         */
        heap = slab_create(cache, behavior & ~ALLOC_ZEROED);
        if (unlikely(heap == nullptr))
            return nullptr;

        state = spin_lock_irq_save(&cache->lock);
        list_insert_next(&cache->partial_slabs, &heap->link);
    }

    node = slist_pop(&heap->freelist);
    heap->num_allocated_objects++;

    // Don't leak the address of the next free object
    node->next = nullptr;

    if (slist_is_empty(&heap->freelist)) {
        list_remove(&heap->link);
        list_insert_next(&cache->full_slabs, &heap->link);
    }

    spin_unlock_irq_restore(&cache->lock, state);
    return node;
}

void *alloc_from_cache(struct alloc_cache *cache, enum alloc_behavior behavior)
{
    void *object;

    object = cache_take_object(cache, behavior);
    if (object != nullptr && (behavior & ALLOC_ZEROED))
        memzero(object, cache->object_size);

    return object;
}

void alloc_cache_destroy(struct alloc_cache *cache)
{
    struct kheap_page *heap;

    // Live objects hold the cache via their page's back pointer
    BUG_ON(!list_is_empty(&cache->partial_slabs));
    BUG_ON(!list_is_empty(&cache->full_slabs));

    while (!list_is_empty(&cache->empty_slabs)) {
        heap = list_first_entry(
            &cache->empty_slabs, struct kheap_page, link
        );
        list_remove(&heap->link);

        free_frozen_block(kheap_to_block(heap));
    }

    cache->num_empty_slabs = 0;
}

void *alloc(size_t size, enum alloc_behavior behavior)
{
    struct alloc_cache *cache;
    size_t cache_idx;

    if (WARN_ON(init_level_below(INIT_LEVEL_HEAP_AVAILABLE)) || size == 0)
        return nullptr;

    if (unlikely(size > BUDDY_MAX_SIZE))
        return nullptr;

    cache_idx = size_to_cache_index(size);

    if (cache_idx >= NUM_BUILTIN_CACHES) {
        struct page_block *block;

        block = alloc_typed_block(
            size_to_order(size, nullptr), behavior, PAGE_TYPE_KHEAP_LARGE
        );
        if (block == nullptr)
            return nullptr;

        return block_to_virt(block);
    }

    cache = &s_builtin_caches[cache_idx];
    return alloc_from_cache(cache, behavior);
}

void free(void *ptr)
{
    struct slist_node *node = ptr;
    struct alloc_cache *cache;
    struct page_block *block;
    struct kheap_page *heap;
    irq_state_t state;
    bool was_full;

    if (ptr == nullptr)
        return;

    block = virt_to_block(ptr);

    /*
     * This is the only validation an erroneous free of a foreign
     * pointer ever gets, keep it on even in non-debug builds.
     */
    BUG_ON(!block_is_kernel_heap(block));

    if (block_type(block) == PAGE_TYPE_KHEAP_LARGE) {
        BUG_ON(ptr != block_to_virt(block));

        // The stored order is the allocation size, nothing to restamp
        free_frozen_block(block);
        return;
    }

    heap = block_kheap(block);
    cache = heap->cache;

    MM_BUG_ON(
        ((char*)ptr - (char*)block_to_virt(block)) % cache->object_size
    );

    state = spin_lock_irq_save(&cache->lock);

    // Catches an immediate double free, not a generic one
    MM_BUG_ON(node == heap->freelist.first);
    MM_BUG_ON(heap->num_allocated_objects == 0);

    was_full = slist_is_empty(&heap->freelist);
    slist_push(&heap->freelist, node);
    heap->num_allocated_objects--;

    if (heap->num_allocated_objects == 0) {
        list_remove(&heap->link);

        if (cache->num_empty_slabs >= cache->max_empty_slabs) {
            spin_unlock_irq_restore(&cache->lock, state);
            free_frozen_block(block);
            return;
        }

        list_insert_next(&cache->empty_slabs, &heap->link);
        cache->num_empty_slabs++;
    } else if (was_full) {
        list_remove(&heap->link);
        list_insert_next(&cache->partial_slabs, &heap->link);
    }

    spin_unlock_irq_restore(&cache->lock, state);
}
