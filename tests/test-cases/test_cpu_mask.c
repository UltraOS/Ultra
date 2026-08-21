#include <cpu_mask.h>

#include <test_harness.h>

TEST_CASE(cpu_mask_set_clear_test_weight)
{
    struct cpu_mask mask = { };

    ASSERT(cpu_mask_empty(&mask));
    ASSERT_EQ(cpu_mask_weight(&mask), 0);

    cpu_mask_set(&mask, 0);
    cpu_mask_set(&mask, 64);
    cpu_mask_set(&mask, ULTRA_MAX_CPUS - 1);

    ASSERT(!cpu_mask_empty(&mask));
    ASSERT_EQ(cpu_mask_weight(&mask), 3);
    ASSERT(cpu_mask_test(&mask, 0));
    ASSERT(cpu_mask_test(&mask, 64));
    ASSERT(cpu_mask_test(&mask, ULTRA_MAX_CPUS - 1));
    ASSERT(!cpu_mask_test(&mask, 1));
    ASSERT(!cpu_mask_test(&mask, 63));

    cpu_mask_clear(&mask, 64);
    ASSERT(!cpu_mask_test(&mask, 64));
    ASSERT_EQ(cpu_mask_weight(&mask), 2);

    cpu_mask_clear_all(&mask);
    ASSERT(cpu_mask_empty(&mask));
    ASSERT_EQ(cpu_mask_weight(&mask), 0);
}

TEST_CASE(cpu_mask_iteration_order)
{
    struct cpu_mask mask = { };
    u32 expected[] = { 2, 63, 64, 100, ULTRA_MAX_CPUS - 1 };
    u32 cpu, i = 0;

    ASSERT_EQ(cpu_mask_first(&mask), ULTRA_MAX_CPUS);

    for (i = 0; i < ARRAY_SIZE(expected); i++)
        cpu_mask_set(&mask, expected[i]);

    i = 0;
    for_each_cpu_in(cpu, &mask) {
        ASSERT(i < ARRAY_SIZE(expected));
        ASSERT_EQ(cpu, expected[i]);
        i++;
    }
    ASSERT_EQ(i, ARRAY_SIZE(expected));

    ASSERT_EQ(cpu_mask_first(&mask), 2);
    ASSERT_EQ(cpu_mask_next(&mask, 2), 63);
    ASSERT_EQ(cpu_mask_next(&mask, ULTRA_MAX_CPUS - 1), ULTRA_MAX_CPUS);
}

TEST_CASE(cpu_mask_and_copy)
{
    struct cpu_mask lhs = { };
    struct cpu_mask rhs = { };
    struct cpu_mask dst = { };

    cpu_mask_set(&lhs, 1);
    cpu_mask_set(&lhs, 70);
    cpu_mask_set(&lhs, 129);

    cpu_mask_set(&rhs, 70);
    cpu_mask_set(&rhs, 129);
    cpu_mask_set(&rhs, 5);

    cpu_mask_and(&dst, &lhs, &rhs);
    ASSERT_EQ(cpu_mask_weight(&dst), 2);
    ASSERT(cpu_mask_test(&dst, 70));
    ASSERT(cpu_mask_test(&dst, 129));
    ASSERT(!cpu_mask_test(&dst, 1));
    ASSERT(!cpu_mask_test(&dst, 5));

    cpu_mask_copy(&lhs, &dst);
    ASSERT_EQ(cpu_mask_weight(&lhs), 2);
    ASSERT(cpu_mask_test(&lhs, 70));
    ASSERT(!cpu_mask_test(&lhs, 1));
}

TEST_CASE(cpu_mask_handle_inline)
{
    cpu_mask_handle handle;

    ASSERT_EQ(cpu_mask_handle_alloc(&handle), EOK);
    ASSERT(cpu_mask_empty(handle));

    cpu_mask_set(handle, 3);
    cpu_mask_set(handle, 100);
    ASSERT_EQ(cpu_mask_weight(handle), 2);
    ASSERT(cpu_mask_test(handle, 3));

    cpu_mask_handle_free(handle);

    // Handles must come back zeroed
    ASSERT_EQ(cpu_mask_handle_alloc(&handle), EOK);
    ASSERT(cpu_mask_empty(handle));
    cpu_mask_handle_free(handle);
}
