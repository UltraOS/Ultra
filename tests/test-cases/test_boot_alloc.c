#include <kernel-source/memory/boot_alloc.c>
#include <test_harness.h>

static void allocator_setup(struct memory_range *ranges, size_t count)
{
    g_buffer = g_initial_buffer;
    g_capacity = BOOT_ALLOC_INITIAL_CAPACITY;
    g_entry_count = count;

    memcpy(g_buffer, ranges, sizeof(*ranges) * count);
}

static void verify_state(struct memory_range *ranges, size_t count)
{
    ASSERT_EQ(g_entry_count, count);

    for (size_t i = 0; i < count; i++) {
        struct memory_range *expected = &ranges[i];
        struct memory_range *actual = &g_buffer[i];

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
    allocator_setup(base_ranges, ARRAY_SIZE(base_ranges))

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

TEST_CASE(middle_split)
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

TEST_CASE(left_mergeable)
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

TEST_CASE(left_non_mergeable)
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

TEST_CASE(right_mergeable)
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

TEST_CASE(right_non_mergeable)
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

TEST_CASE(entire_non_mergable)
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

TEST_CASE(entire_mergable)
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

TEST_CASE(entire_left_mergable)
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

TEST_CASE(entire_right_mergable)
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

TEST_CASE(alloc_at_oom)
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

TEST_CASE(alloc_top_down)
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

TEST_CASE(buffer_growth)
{
    BASE_STATE(
        RANGE(0x1000, 0x3000, MEMORY_FREE),
    );

    malloc_phys_range(0x1000, 0x3000);
    g_capacity = 2;

    // Will grab 0x3000 -> 0x4000 to resize itself
    ALLOC_EXPECT(1, 0x2000);
    ASSERT_EQ((uintptr_t)g_buffer, (uintptr_t)phys_to_virt(0x3000));

    CHECK_STATE(
        RANGE(0x1000, 0x1000, MEMORY_FREE),
        RANGE(0x2000, 0x2000, MEMORY_ALLOCATED),
    );
}
