#include <kernel-source/memory/boot_alloc.c>
#undef MSG_FMT

#include <kernel-source/memory/buddy.c>
#undef MSG_FMT

#define free test_free
#include <kernel-source/memory/alloc.c>

#include <test_harness.h>

static void boot_allocator_setup(struct memory_range *ranges, size_t count)
{
    s_buffer = s_initial_buffer;
    s_capacity = BOOT_ALLOC_INITIAL_CAPACITY;
    s_entry_count = count;

    memcpy(s_buffer, ranges, sizeof(*ranges) * count);
}

static void verify_state(struct memory_range *ranges, size_t count)
{
    ASSERT_EQ(s_entry_count, count);

    for (size_t i = 0; i < count; i++) {
        struct memory_range *expected = &ranges[i];
        struct memory_range *actual = &s_buffer[i];

        ASSERT_EQ(expected->physical_address, actual->physical_address);
        ASSERT_EQ(MR_SIZE(expected), MR_SIZE(actual));
        ASSERT_EQ(MR_TYPE(expected), MR_TYPE(actual));
    }
}

#define RANGE(start, length, type) { start, MR_ENCODE(length, type) }
#define BASE_STATE(...)                        \
    struct memory_range base_ranges[] = {      \
        __VA_ARGS__                            \
    };                                         \
    boot_allocator_setup(base_ranges, ARRAY_SIZE(base_ranges))

#define DO_CHECK_STATE(line, ...)                            \
    struct memory_range CONCAT(expected_state, line)[] = {   \
        __VA_ARGS__                                          \
    };                                                       \
    verify_state(CONCAT(expected_state, line),               \
                 ARRAY_SIZE(CONCAT(expected_state, line)))

#define CHECK_STATE(...) DO_CHECK_STATE(__LINE__, __VA_ARGS__)

#define ALLOC_PAGES_AT_ASSERT_EQ(value, num_pages, expect) \
    do {                                                   \
        phys_addr_t ret = boot_alloc_at(value, num_pages); \
        ASSERT_EQ(ret, expect);                            \
    } while (0)

#define ALLOC_PAGE_AT_ASSERT(value) \
    ALLOC_PAGES_AT_ASSERT_EQ(value, 1, value)

#define ALLOC_PAGES_AT_ASSERT_OOM(value, num_pages) \
    ALLOC_PAGES_AT_ASSERT_EQ(value, num_pages, encode_error_phys_addr(ENOMEM))

#define ALLOC_EXPECT(num_pages, expect)          \
        do {                                     \
        phys_addr_t ret = boot_alloc(num_pages); \
        ASSERT_EQ(ret, expect);                  \
} while (0)

TEST_CASE(boot_alloc_middle_split)
{
    BASE_STATE(
        RANGE(0x1000, 0x3000, MEMORY_FREE),
    );

    ALLOC_PAGE_AT_ASSERT(0x2000);

    CHECK_STATE(
        RANGE(0x1000, 0x1000, MEMORY_FREE),
        RANGE(0x2000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x3000, 0x1000, MEMORY_FREE),
    );
}

TEST_CASE(boot_alloc_left_mergeable)
{
    BASE_STATE(
        RANGE(0x1000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x2000, 0x2000, MEMORY_FREE),
    );

    ALLOC_PAGE_AT_ASSERT(0x2000);

    CHECK_STATE(
        RANGE(0x1000, 0x2000, MEMORY_ALLOCATED),
        RANGE(0x3000, 0x1000, MEMORY_FREE),
    );
}

TEST_CASE(boot_alloc_left_non_mergeable)
{
    BASE_STATE(
        RANGE(0x0000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x2000, 0x2000, MEMORY_FREE),
    );

    ALLOC_PAGE_AT_ASSERT(0x2000);

    CHECK_STATE(
        RANGE(0x0000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x2000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x3000, 0x1000, MEMORY_FREE),
    );
}

TEST_CASE(boot_alloc_right_mergeable)
{
    BASE_STATE(
        RANGE(0x1000, 0x2000, MEMORY_FREE),
        RANGE(0x3000, 0x1000, MEMORY_ALLOCATED),
    );

    ALLOC_PAGE_AT_ASSERT(0x2000);

    CHECK_STATE(
        RANGE(0x1000, 0x1000, MEMORY_FREE),
        RANGE(0x2000, 0x2000, MEMORY_ALLOCATED),
    );
}

TEST_CASE(boot_alloc_right_non_mergeable)
{
    BASE_STATE(
        RANGE(0x1000, 0x2000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
    );

    ALLOC_PAGE_AT_ASSERT(0x2000);

    CHECK_STATE(
        RANGE(0x1000, 0x1000, MEMORY_FREE),
        RANGE(0x2000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
    );
}

TEST_CASE(boot_alloc_entire_non_mergable)
{
    BASE_STATE(
        RANGE(0x0000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x2000, 0x1000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
    );

    ALLOC_PAGE_AT_ASSERT(0x2000);

    CHECK_STATE(
        RANGE(0x0000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x2000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
    );
}

TEST_CASE(boot_alloc_entire_mergable)
{
    BASE_STATE(
        RANGE(0x0000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x1000, 0x1000, MEMORY_FREE),
        RANGE(0x2000, 0x1000, MEMORY_ALLOCATED),
    );

    ALLOC_PAGE_AT_ASSERT(0x1000);

    CHECK_STATE(
        RANGE(0x0000, 0x3000, MEMORY_ALLOCATED),
    );
}

TEST_CASE(boot_alloc_entire_left_mergable)
{
    BASE_STATE(
        RANGE(0x0000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x1000, 0x1000, MEMORY_FREE),
        RANGE(0x3000, 0x1000, MEMORY_ALLOCATED),
    );

    ALLOC_PAGE_AT_ASSERT(0x1000);

    CHECK_STATE(
        RANGE(0x0000, 0x2000, MEMORY_ALLOCATED),
        RANGE(0x3000, 0x1000, MEMORY_ALLOCATED),
    );
}

TEST_CASE(boot_alloc_entire_right_mergable)
{
    BASE_STATE(
        RANGE(0x0000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x2000, 0x1000, MEMORY_FREE),
        RANGE(0x3000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x5000, 0x1000, MEMORY_ALLOCATED),
    );

    ALLOC_PAGE_AT_ASSERT(0x2000);

    CHECK_STATE(
        RANGE(0x0000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x2000, 0x2000, MEMORY_ALLOCATED),
        RANGE(0x5000, 0x1000, MEMORY_ALLOCATED),
    );
}

TEST_CASE(boot_alloc_at_oom)
{
    BASE_STATE(
        RANGE(0x2000, 0x1000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x6000, 0x1000, MEMORY_FREE),
        RANGE(0x8000, 0x2000, MEMORY_FREE),
    );

    ALLOC_PAGES_AT_ASSERT_OOM(0x4000, 1);
    ALLOC_PAGES_AT_ASSERT_OOM(0x2000, 2);
    ALLOC_PAGES_AT_ASSERT_OOM(0x6000, 2);
    ALLOC_PAGES_AT_ASSERT_OOM(0x0000, 1);
    ALLOC_PAGES_AT_ASSERT_OOM(0x10000, 1);
    ALLOC_PAGES_AT_ASSERT_OOM(0x7000, 2);
    ALLOC_PAGES_AT_ASSERT_OOM(0x8000, 3);
    ALLOC_PAGES_AT_ASSERT_OOM(0x3000, 1);

    CHECK_STATE(
        RANGE(0x2000, 0x1000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x6000, 0x1000, MEMORY_FREE),
        RANGE(0x8000, 0x2000, MEMORY_FREE),
    );
}

TEST_CASE(boot_alloc_top_down)
{
    BASE_STATE(
        RANGE(0x2000, 0x2000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x6000, 0x1000, MEMORY_FREE),
        RANGE(0x8000, 0x3000, MEMORY_FREE),
    );

    ALLOC_EXPECT(1, 0xA000);
    CHECK_STATE(
        RANGE(0x2000, 0x2000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x6000, 0x1000, MEMORY_FREE),
        RANGE(0x8000, 0x2000, MEMORY_FREE),
        RANGE(0xA000, 0x1000, MEMORY_ALLOCATED),
    );

    ALLOC_EXPECT(2, 0x8000);
    CHECK_STATE(
        RANGE(0x2000, 0x2000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x6000, 0x1000, MEMORY_FREE),
        RANGE(0x8000, 0x3000, MEMORY_ALLOCATED),
    );

    ALLOC_EXPECT(2, 0x2000);
    CHECK_STATE(
        RANGE(0x2000, 0x3000, MEMORY_ALLOCATED),
        RANGE(0x6000, 0x1000, MEMORY_FREE),
        RANGE(0x8000, 0x3000, MEMORY_ALLOCATED),
    );

    ALLOC_EXPECT(2, encode_error_phys_addr(ENOMEM));

    ALLOC_EXPECT(1, 0x6000);
    CHECK_STATE(
        RANGE(0x2000, 0x3000, MEMORY_ALLOCATED),
        RANGE(0x6000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x8000, 0x3000, MEMORY_ALLOCATED),
    );

    ALLOC_EXPECT(1, encode_error_phys_addr(ENOMEM));
}

TEST_CASE(boot_alloc_buffer_growth)
{
    BASE_STATE(
        RANGE(0x1000, 0x3000, MEMORY_FREE),
    );

    malloc_phys_range(0x1000, 0x3000);
    s_capacity = 2;

    // Will grab 0x3000 -> 0x4000 to resize itself
    ALLOC_EXPECT(1, 0x2000);
    ASSERT_EQ((uintptr_t)s_buffer, (uintptr_t)phys_to_virt(0x3000));

    CHECK_STATE(
        RANGE(0x1000, 0x1000, MEMORY_FREE),
        RANGE(0x2000, 0x2000, MEMORY_ALLOCATED),
    );
}

TEST_CASE(boot_alloc_buffer_growth_multi)
{
    BASE_STATE(
        RANGE(0x1000, 0x10000, MEMORY_FREE),
    );

    // Map the physical memory so phys_to_virt works inside the allocator
    malloc_phys_range(0x1000, 0x10000);

    // Only one spare slot
    s_capacity = 2;

    ALLOC_EXPECT(1, 0xF000);

    // Ensure we actually migrated away from the static buffer
    ASSERT_NE(s_buffer, s_initial_buffer);

    // 2 spare slots, to trigger a resize again
    s_capacity = s_entry_count + 2;

    ALLOC_EXPECT(1, 0x10000);

    // Verify the allocator merged the internal arrays
    CHECK_STATE(
        RANGE(0x1000, 0xD000, MEMORY_FREE),
        RANGE(0xE000, 0x3000, MEMORY_ALLOCATED),
    );
}

#define ALLOC_ALIGNED_EXPECT(num_pages, alignment, expect)          \
    do {                                                            \
        phys_addr_t ret = boot_alloc_aligned(num_pages, alignment); \
        ASSERT_EQ(ret, expect);                                     \
    } while (0)

TEST_CASE(boot_alloc_aligned_middle_split)
{
    BASE_STATE(
        RANGE(0x1000, 0x6000, MEMORY_FREE),
    );

    /*
     * Request 1 page (0x1000), aligned to 0x4000
     * allocator logic: ALIGN_DOWN(0x7000 - 0x1000, 0x4000) = 0x4000
     * It should carve out 0x4000 -> 0x5000
     */
    ALLOC_ALIGNED_EXPECT(1, 0x4000, 0x4000);

    CHECK_STATE(
        RANGE(0x1000, 0x3000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x5000, 0x2000, MEMORY_FREE),
    );
}

TEST_CASE(boot_alloc_aligned_bottom_match)
{
    BASE_STATE(
        RANGE(0x4000, 0x3000, MEMORY_FREE),
    );

    /*
     * Request 2 pages (0x2000), aligned to 0x4000
     * ALIGN_DOWN(0x7000 - 0x2000, 0x4000) = 0x4000
     * Carves out 0x4000 -> 0x6000
     */
    ALLOC_ALIGNED_EXPECT(2, 0x4000, 0x4000);

    CHECK_STATE(
        RANGE(0x4000, 0x2000, MEMORY_ALLOCATED),
        RANGE(0x6000, 0x1000, MEMORY_FREE),
    );
}

TEST_CASE(boot_alloc_aligned_top_match)
{
    BASE_STATE(
        RANGE(0x1000, 0x4000, MEMORY_FREE),
    );

    /*
     * Request 1 page (0x1000), aligned to 0x4000
     * ALIGN_DOWN(0x5000 - 0x1000, 0x4000) = 0x4000
     * Carves out 0x4000 -> 0x5000
     */
    ALLOC_ALIGNED_EXPECT(1, 0x4000, 0x4000);

    CHECK_STATE(
        RANGE(0x1000, 0x3000, MEMORY_FREE),
        RANGE(0x4000, 0x1000, MEMORY_ALLOCATED),
    );
}

TEST_CASE(boot_alloc_aligned_oom)
{
    BASE_STATE(
        RANGE(0x1000, 0x2000, MEMORY_FREE),
        RANGE(0x5000, 0x2000, MEMORY_FREE),
    );

    ALLOC_ALIGNED_EXPECT(1, 0x4000, encode_error_phys_addr(ENOMEM));

    CHECK_STATE(
        RANGE(0x1000, 0x2000, MEMORY_FREE),
        RANGE(0x5000, 0x2000, MEMORY_FREE),
    );
}

static void allocator_state_setup(struct memory_range *ranges, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
        malloc_phys_range(ranges[i].physical_address, MR_SIZE(&ranges[i]));

    boot_allocator_setup(ranges, count);
    buddy_setup();
    kernel_heap_init();
}

#define FREE_RANGE(start, length) { start, MR_ENCODE(length, MEMORY_FREE) }
#define BASE_ALLOCATOR_STATE(...)         \
    struct memory_range base_ranges[] = { \
        __VA_ARGS__                       \
    };                                    \
    allocator_state_setup(base_ranges, ARRAY_SIZE(base_ranges))

#define ASSERT_ORDER_FREE(order, count) \
    ASSERT_EQ(s_ctx.free_areas[(order)].num_free, (size_t)(count))

static size_t list_len(struct list_link *head)
{
    size_t count = 0;
    struct list_link *cursor;

    list_for_each(cursor, head)
        count++;

    return count;
}

/*
 * A region that is a power-of-two number of pages and aligned to that size
 * collapses into exactly one block of the matching order.
 */
TEST_CASE(buddy_init_single_block)
{
    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x4000),
    );

    ASSERT_ORDER_FREE(0, 0);
    ASSERT_ORDER_FREE(1, 0);
    ASSERT_ORDER_FREE(2, 1);
    ASSERT_ORDER_FREE(3, 0);
}

/*
 * The biggest block the buddy allocator can track is BUDDY_MAX_ORDER. A region
 * exactly that size must collapse into a single max-order block.
 */
TEST_CASE(buddy_init_max_order_block)
{
    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, PAGE_SIZE << BUDDY_MAX_ORDER),
    );

    for (u8 order = 0; order < BUDDY_MAX_ORDER; order++)
        ASSERT_ORDER_FREE(order, 0);

    ASSERT_ORDER_FREE(BUDDY_MAX_ORDER, 1);

    ASSERT(alloc_block(BUDDY_MAX_ORDER, ALLOC_GENERIC) != nullptr);
    ASSERT_ORDER_FREE(BUDDY_MAX_ORDER, 0);
}

/*
 * A region whose size/alignment is not a single power of two is carved into
 * the largest naturally-aligned blocks that fit. [0x1000, 0x4000) becomes one
 * order-0 page (0x1000) and one order-1 block (0x2000..0x4000).
 */
TEST_CASE(buddy_init_split_region)
{
    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x1000, 0x3000),
    );

    ASSERT_ORDER_FREE(0, 1);
    ASSERT_ORDER_FREE(1, 1);
    ASSERT_ORDER_FREE(2, 0);
}

/*
 * Reserved ranges must never enter the free lists, and the free pages on
 * either side of them must not coalesce across the reserved gap.
 */
TEST_CASE(buddy_init_reserved_gap)
{
    struct page *reserved;

    BASE_ALLOCATOR_STATE(
        RANGE(0x0000, 0x1000, MEMORY_FREE),
        RANGE(0x1000, 0x1000, MEMORY_ALLOCATED),
        RANGE(0x2000, 0x1000, MEMORY_FREE),
    );

    ASSERT_ORDER_FREE(0, 2);
    ASSERT_ORDER_FREE(1, 0);

    reserved = pfn_to_page(0x1000 >> PAGE_SHIFT);
    ASSERT_EQ(page_type(reserved), PAGE_TYPE_RESERVED);
}

// Allocating at the exact order present simply removes the block.
TEST_CASE(buddy_alloc_exact_order)
{
    struct page_block *block;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x4000),
    );

    block = alloc_block(2, ALLOC_GENERIC);
    ASSERT(block != nullptr);
    ASSERT_EQ(page_to_pfn(block_to_page(block)), 0);
    ASSERT_EQ(block_type(block), PAGE_TYPE_GENERIC);
    ASSERT_EQ(block_num_references(block), 1);

    ASSERT_ORDER_FREE(2, 0);
}

/*
 * Allocating a smaller order than is available splits the larger block in half
 * repeatedly, leaving one free block at each intermediate order.
 */
TEST_CASE(buddy_alloc_splits_down)
{
    struct page_block *block;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x4000),
    );

    block = alloc_block(0, ALLOC_GENERIC);
    ASSERT(block != nullptr);
    ASSERT_EQ(page_to_pfn(block_to_page(block)), 0);

    ASSERT_ORDER_FREE(0, 1);
    ASSERT_ORDER_FREE(1, 1);
    ASSERT_ORDER_FREE(2, 0);
}

// Freeing a block whose buddy is also free coalesces all the way back up.
TEST_CASE(buddy_free_coalesces_up)
{
    struct page_block *block;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x4000),
    );

    block = alloc_block(0, ALLOC_GENERIC);
    ASSERT_ORDER_FREE(2, 0);

    // Drops the single reference from alloc_block(), freeing the block.
    block_unref(block);

    ASSERT_ORDER_FREE(0, 0);
    ASSERT_ORDER_FREE(1, 0);
    ASSERT_ORDER_FREE(2, 1);
}

/*
 * A block must not coalesce while its buddy is still allocated. Only once both
 * halves are free may they merge back into the parent order.
 */
TEST_CASE(buddy_free_no_coalesce_busy_buddy)
{
    struct page_block *first, *second;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x2000),
    );

    first = alloc_block(0, ALLOC_GENERIC);
    second = alloc_block(0, ALLOC_GENERIC);
    ASSERT(first != nullptr && second != nullptr);
    ASSERT_NE(page_to_pfn(block_to_page(first)),
              page_to_pfn(block_to_page(second)));

    // Buddy of 'first' is still allocated, so this stays an order-0 block.
    block_unref(first);
    ASSERT_ORDER_FREE(0, 1);
    ASSERT_ORDER_FREE(1, 0);

    // Now both halves are free and must merge.
    block_unref(second);
    ASSERT_ORDER_FREE(0, 0);
    ASSERT_ORDER_FREE(1, 1);
}

// Requesting an order beyond what the allocator supports fails cleanly.
TEST_CASE(buddy_alloc_order_too_large)
{
    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    ASSERT_EQ(alloc_block(BUDDY_MAX_ORDER + 1, ALLOC_GENERIC), NULL);

    // The valid memory is untouched.
    ASSERT_ORDER_FREE(0, 1);
}

// Once every page is handed out, further allocations return null.
TEST_CASE(buddy_alloc_exhaustion)
{
    struct page_block *blocks[4];
    size_t i, j;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x4000),
    );

    for (i = 0; i < ARRAY_SIZE(blocks); i++) {
        blocks[i] = alloc_block(0, ALLOC_GENERIC);
        ASSERT(blocks[i] != nullptr);

        for (j = 0; j < i; j++)
            ASSERT_NE(page_to_pfn(block_to_page(blocks[i])),
                      page_to_pfn(block_to_page(blocks[j])));
    }

    ASSERT_EQ(alloc_block(0, ALLOC_GENERIC), NULL);
}

/*
 * A higher-order request cannot be satisfied from only lower-order free blocks
 * even when enough total pages exist.
 */
TEST_CASE(buddy_alloc_no_merge_for_higher_order)
{
    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    ASSERT_ORDER_FREE(0, 1);
    ASSERT_EQ(alloc_block(1, ALLOC_GENERIC), NULL);
    ASSERT_ORDER_FREE(0, 1);
}

// The size of a small-object allocation that yields a 2-object slab.
#define SLAB_2K 2048

TEST_CASE(slab_size_to_cache_index)
{
    // Everything up to the minimum cache size lands in the first cache.
    ASSERT_EQ(size_to_cache_index(1), 0);
    ASSERT_EQ(size_to_cache_index(MIN_CACHE_SIZE), 0);

    // Just past a power of two bumps up to the next cache.
    ASSERT_EQ(size_to_cache_index(MIN_CACHE_SIZE + 1), 1);
    ASSERT_EQ(size_to_cache_index(16), 1);
    ASSERT_EQ(size_to_cache_index(17), 2);
    ASSERT_EQ(size_to_cache_index(32), 2);

    // The largest builtin cache is exactly 8K.
    ASSERT_EQ(size_to_cache_index(8192), NUM_BUILTIN_CACHES - 1);

    // Anything bigger spills past the builtin range (served by the buddy).
    ASSERT(size_to_cache_index(8193) >= NUM_BUILTIN_CACHES);
}

TEST_CASE(slab_size_to_order)
{
    size_t allocation_size = 0;

    ASSERT_EQ(size_to_order(1, &allocation_size), 0);
    ASSERT_EQ(allocation_size, PAGE_SIZE);

    ASSERT_EQ(size_to_order(PAGE_SIZE, &allocation_size), 0);
    ASSERT_EQ(allocation_size, PAGE_SIZE);

    ASSERT_EQ(size_to_order(PAGE_SIZE + 1, &allocation_size), 1);
    ASSERT_EQ(allocation_size, PAGE_SIZE * 2);

    ASSERT_EQ(size_to_order(PAGE_SIZE * 2, &allocation_size), 1);
    ASSERT_EQ(allocation_size, PAGE_SIZE * 2);

    // The out-parameter is optional.
    ASSERT_EQ(size_to_order(PAGE_SIZE * 2 + 1, nullptr), 2);
}

/*
 * A first allocation lazily provisions a slab from the buddy allocator, marks
 * the backing page as heap-owned, and parks it on the partial list.
 */
TEST_CASE(slab_alloc_provisions_slab)
{
    struct alloc_cache *cache;
    struct page *page;
    void *obj;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    obj = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(obj != nullptr);

    cache = &s_builtin_caches[size_to_cache_index(SLAB_2K)];
    page = virt_to_page(obj);

    ASSERT_EQ(page_type(page), PAGE_TYPE_KHEAP);
    ASSERT_EQ((uintptr_t)page->kheap.cache, (uintptr_t)cache);
    ASSERT_EQ(page->kheap.num_allocated_objects, 1);

    // The object lives inside its backing page and is properly aligned.
    ASSERT_EQ((uintptr_t)obj % cache->object_size, 0);
    ASSERT(obj >= page_to_virt(page));

    ASSERT_EQ(list_len(&cache->partial_slabs), 1);
    ASSERT_EQ(list_len(&cache->full_slabs), 0);

    // The slab consumed the only buddy page.
    ASSERT_ORDER_FREE(0, 0);
}

// A zero-sized allocation is rejected.
TEST_CASE(slab_alloc_zero_size)
{
    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    ASSERT_EQ(alloc(0, ALLOC_GENERIC), NULL);
}

// Sizes beyond the largest buddy block are refused up front.
TEST_CASE(slab_alloc_too_large)
{
    size_t size;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    size = BUDDY_MAX_SIZE + 1;
    ASSERT_EQ(alloc(size, ALLOC_GENERIC), NULL);
    ASSERT_EQ(alloc((size_t)-1, ALLOC_GENERIC), NULL);
}

// Allocations are refused before the heap has been brought online.
TEST_CASE(slab_alloc_offline)
{
    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    s_test_init_level = INIT_LEVEL_BUDDY_AVAILABLE;
    ASSERT_EQ(alloc(SLAB_2K, ALLOC_GENERIC), NULL);
    s_test_init_level = NUM_INIT_LEVELS;
}

// ALLOC_ZEROED must clear the object even when reusing dirty freed memory.
TEST_CASE(slab_alloc_zeroed)
{
    struct alloc_cache *cache;
    unsigned char *obj;
    size_t i;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    cache = &s_builtin_caches[size_to_cache_index(SLAB_2K)];

    // Dirty the backing storage, then release it back to the freelist.
    obj = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(obj != nullptr);
    memset(obj, 0xFF, cache->object_size);
    free(obj);

    obj = alloc(SLAB_2K, ALLOC_ZEROED);
    ASSERT(obj != nullptr);

    for (i = 0; i < cache->object_size; i++)
        ASSERT_EQ(obj[i], 0);
}

// The freelist link of a neighboring free object must not leak.
TEST_CASE(slab_alloc_wipes_freelist_pointer)
{
    void *a, *b;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    a = alloc(SLAB_2K, ALLOC_GENERIC);
    b = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(a != nullptr && b != nullptr);

    free(b);
    free(a);

    a = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT_EQ(*(uintptr_t*)a, 0);
}

// Objects handed out from one slab are distinct and accounted for.
TEST_CASE(slab_alloc_distinct_objects)
{
    void *a, *b;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    a = alloc(SLAB_2K, ALLOC_GENERIC);
    b = alloc(SLAB_2K, ALLOC_GENERIC);

    ASSERT(a != nullptr && b != nullptr);
    ASSERT_NE((uintptr_t)a, (uintptr_t)b);
    ASSERT_EQ(virt_to_page(a)->kheap.num_allocated_objects, 2);
}

// The per-slab freelist is LIFO: a freed object is the next one handed back.
TEST_CASE(slab_freelist_is_lifo)
{
    void *a, *b, *c;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    a = alloc(SLAB_2K, ALLOC_GENERIC);
    b = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(a != nullptr && b != nullptr);

    free(a);
    c = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT_EQ((uintptr_t)c, (uintptr_t)a);
}

/*
 * Once a slab is fully consumed it migrates to the full list, and a subsequent
 * allocation provisions a brand new slab.
 */
TEST_CASE(slab_full_slab_spawns_new)
{
    struct alloc_cache *cache;
    void *a, *b, *c;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x2000),
    );

    cache = &s_builtin_caches[size_to_cache_index(SLAB_2K)];

    // Two objects exactly fill a 2K slab backed by a single 4K page.
    a = alloc(SLAB_2K, ALLOC_GENERIC);
    b = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(a != nullptr && b != nullptr);

    ASSERT_EQ(list_len(&cache->full_slabs), 1);
    ASSERT_EQ(list_len(&cache->partial_slabs), 0);

    c = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(c != nullptr);
    ASSERT_EQ(list_len(&cache->full_slabs), 1);
    ASSERT_EQ(list_len(&cache->partial_slabs), 1);

    // The two slabs are backed by different pages.
    ASSERT_NE(page_to_pfn(virt_to_page(a)), page_to_pfn(virt_to_page(c)));
}

// Freeing from a full slab returns it to the partial list.
TEST_CASE(slab_free_full_to_partial)
{
    struct alloc_cache *cache;
    void *a, *b;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    cache = &s_builtin_caches[size_to_cache_index(SLAB_2K)];

    a = alloc(SLAB_2K, ALLOC_GENERIC);
    b = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(a != nullptr && b != nullptr);
    ASSERT_EQ(list_len(&cache->full_slabs), 1);

    free(a);
    ASSERT_EQ(list_len(&cache->full_slabs), 0);
    ASSERT_EQ(list_len(&cache->partial_slabs), 1);
    ASSERT_EQ(virt_to_page(b)->kheap.num_allocated_objects, 1);
}

/*
 * Emptying a slab keeps it cached (rather than freed) while the cache is below
 * its empty-slab watermark, and the cached slab is reused on the next alloc
 * without touching the buddy allocator.
 */
TEST_CASE(slab_empty_slab_is_retained_and_reused)
{
    struct alloc_cache *cache;
    void *obj;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    cache = &s_builtin_caches[size_to_cache_index(SLAB_2K)];

    obj = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(obj != nullptr);
    ASSERT_ORDER_FREE(0, 0);

    free(obj);
    ASSERT_EQ(cache->num_empty_slabs, 1);
    ASSERT_EQ(list_len(&cache->empty_slabs), 1);
    ASSERT_EQ(list_len(&cache->partial_slabs), 0);
    // Page was retained by the cache, not returned to the buddy.
    ASSERT_ORDER_FREE(0, 0);

    obj = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT(obj != nullptr);
    ASSERT_EQ(cache->num_empty_slabs, 0);
    ASSERT_EQ(list_len(&cache->empty_slabs), 0);
    ASSERT_EQ(list_len(&cache->partial_slabs), 1);
    ASSERT_ORDER_FREE(0, 0);
}

/*
 * Past the empty-slab watermark, an emptied slab is released back to the buddy
 * allocator instead of being cached.
 */
TEST_CASE(slab_empty_slab_eviction)
{
    struct alloc_cache cache;
    struct page *evicted;
    void *a, *b, *c, *d;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x2000),
    );

    /*
     * Builtin caches retain at least 8 empty slabs, which is awkward to drive.
     * Use a private cache and force the watermark down to a single slab so the
     * eviction path is reached after just two emptied slabs.
     */
    alloc_cache_init(&cache, "test_evict", SLAB_2K, 0);
    cache.max_empty_slabs = 1;

    // Fill two separate slabs (two objects each).
    a = alloc_from_cache(&cache, ALLOC_GENERIC);
    b = alloc_from_cache(&cache, ALLOC_GENERIC);
    c = alloc_from_cache(&cache, ALLOC_GENERIC);
    d = alloc_from_cache(&cache, ALLOC_GENERIC);
    ASSERT(a != nullptr && b != nullptr && c != nullptr && d != nullptr);

    evicted = virt_to_page(c);

    free(a);
    free(b);
    // First emptied slab fits under the watermark and is retained.
    ASSERT_EQ(cache.num_empty_slabs, 1);

    free(c);
    free(d);
    // Second emptied slab exceeds the watermark and goes back to the buddy.
    ASSERT_EQ(cache.num_empty_slabs, 1);
    ASSERT_EQ(list_len(&cache.empty_slabs), 1);
    ASSERT_EQ(list_len(&cache.partial_slabs), 0);
    ASSERT_EQ(list_len(&cache.full_slabs), 0);
    ASSERT_EQ(page_type(evicted), PAGE_TYPE_BUDDY);
    ASSERT_ORDER_FREE(0, 1);
}

/*
 * Allocations larger than the biggest builtin cache bypass the slabs entirely
 * and are served as raw buddy pages, then returned on free.
 */
TEST_CASE(slab_large_alloc_via_buddy)
{
    struct page *page;
    void *obj;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x4000),
    );

    // 0x4000 bytes -> order 2 (4 pages), past the 8K builtin ceiling.
    obj = alloc(0x4000, ALLOC_GENERIC);
    ASSERT(obj != nullptr);

    page = virt_to_page(obj);
    ASSERT_EQ(page_type(page), PAGE_TYPE_KHEAP_LARGE);
    ASSERT_EQ(page_to_pfn(page), 0);
    ASSERT_ORDER_FREE(2, 0);

    free(obj);
    ASSERT_ORDER_FREE(2, 1);
}

/*
 * A large allocation that has to split a bigger buddy block must, on free,
 * return exactly the pages that were allocated and coalesce back to the
 * original block. This exercises whether the allocated order is tracked
 * correctly across a split.
 */
TEST_CASE(slab_large_alloc_split_roundtrip)
{
    void *obj;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x8000),
    );

    ASSERT_ORDER_FREE(3, 1);

    // order-2 request carved out of the order-3 block.
    obj = alloc(0x4000, ALLOC_GENERIC);
    ASSERT(obj != nullptr);
    ASSERT_ORDER_FREE(3, 0);
    ASSERT_ORDER_FREE(2, 1);

    free(obj);

    // Everything should coalesce back into the single order-3 block.
    ASSERT_ORDER_FREE(2, 0);
    ASSERT_ORDER_FREE(3, 1);
}

// When the buddy allocator is empty, slab allocation fails gracefully.
TEST_CASE(slab_alloc_out_of_memory)
{
    void *obj;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    // Drain the only page through the buddy directly.
    ASSERT(alloc_page(0) != nullptr);
    ASSERT_ORDER_FREE(0, 0);

    obj = alloc(SLAB_2K, ALLOC_GENERIC);
    ASSERT_EQ(obj, NULL);
}

/*
 * The object stride is padded up to the requested alignment, and every
 * object handed out honors it (slabs are naturally aligned, so a
 * multiple-of-align stride is all it takes).
 */
TEST_CASE(alloc_cache_alignment)
{
    struct alloc_cache cache;
    void *a, *b;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    alloc_cache_init(&cache, "test_align", 40, 64);
    ASSERT_EQ(cache.object_size, 64);

    a = alloc_from_cache(&cache, ALLOC_GENERIC);
    b = alloc_from_cache(&cache, ALLOC_GENERIC);
    ASSERT(a != nullptr && b != nullptr);
    ASSERT_NE((uintptr_t)a, (uintptr_t)b);
    ASSERT_EQ((uintptr_t)a % 64, 0);
    ASSERT_EQ((uintptr_t)b % 64, 0);

    free(a);
    free(b);
}

/*
 * The slab order is picked by bounding tail waste, not by the smallest
 * fit: a 1.5K object wastes a quarter of an order-0 slab but only 1/16
 * of an order-1 one.
 */
TEST_CASE(alloc_cache_order_bounds_waste)
{
    struct alloc_cache cache;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    alloc_cache_init(&cache, "test_waste", 1536, 0);
    ASSERT_EQ(cache.order, 1);

    // Power-of-two sizes pack exactly and keep the smallest fit.
    alloc_cache_init(&cache, "test_exact", SLAB_2K, 0);
    ASSERT_EQ(cache.order, 0);
}

// ALLOC_ZEROED through a dedicated cache clears recycled objects too.
TEST_CASE(alloc_cache_alloc_zeroed)
{
    struct alloc_cache cache;
    unsigned char *obj;
    size_t i;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    alloc_cache_init(&cache, "test_zeroed", SLAB_2K, 0);

    obj = alloc_from_cache(&cache, ALLOC_GENERIC);
    ASSERT(obj != nullptr);
    memset(obj, 0xFF, cache.object_size);
    free(obj);

    obj = alloc_from_cache(&cache, ALLOC_ZEROED);
    ASSERT(obj != nullptr);

    for (i = 0; i < cache.object_size; i++)
        ASSERT_EQ(obj[i], 0);

    free(obj);
}

// Destroying a cache hands its retained empty slabs back to the buddy.
TEST_CASE(alloc_cache_destroy_releases_slabs)
{
    struct alloc_cache cache;
    struct page *slab_page;
    void *obj;

    BASE_ALLOCATOR_STATE(
        FREE_RANGE(0x0000, 0x1000),
    );

    alloc_cache_init(&cache, "test_destroy", SLAB_2K, 0);

    obj = alloc_from_cache(&cache, ALLOC_GENERIC);
    ASSERT(obj != nullptr);
    slab_page = virt_to_page(obj);

    free(obj);
    ASSERT_EQ(cache.num_empty_slabs, 1);
    ASSERT_ORDER_FREE(0, 0);

    alloc_cache_destroy(&cache);
    ASSERT_EQ(cache.num_empty_slabs, 0);
    ASSERT_EQ(list_len(&cache.empty_slabs), 0);
    ASSERT_EQ(page_type(slab_page), PAGE_TYPE_BUDDY);
    ASSERT_ORDER_FREE(0, 1);
}
