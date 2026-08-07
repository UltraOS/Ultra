#include <kernel-source/memory/boot_alloc.c>
#undef MSG_FMT

#include <kernel-source/memory/buddy.c>

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
