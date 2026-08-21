#define CONFIG_CPU_MASK_OUT_OF_LINE 1

/*
 * Reroute the kernel heap so the allocation size is observable and no
 * real allocator bring-up is needed.
 */
#define alloc cpu_mask_test_alloc
#define free cpu_mask_test_free

#include <kernel-source/cpu_mask.c>

#undef alloc
#undef free

#include <stdlib.h>
#include <string.h>

#include <test_harness.h>

u32 g_num_present_cpus;

static size_t s_last_alloc_size;

void *cpu_mask_test_alloc(size_t size, enum alloc_behavior behavior)
{
    void *ptr;

    s_last_alloc_size = size;

    ptr = malloc(size);
    if (ptr != NULL && (behavior & ALLOC_ZEROED))
        memset(ptr, 0, size);

    return ptr;
}

void cpu_mask_test_free(void *ptr)
{
    free(ptr);
}

TEST_CASE(cpu_mask_out_of_line_alloc_is_sized_by_present_cpus)
{
    cpu_mask_handle handle;

    g_num_present_cpus = 70;

    ASSERT_EQ(cpu_mask_handle_alloc(&handle), EOK);
    ASSERT_EQ(s_last_alloc_size, NUM_UNITS_FOR_BITS(70) * sizeof(reg_t));
    ASSERT(cpu_mask_empty(handle));

    cpu_mask_set(handle, 69);
    ASSERT(cpu_mask_test(handle, 69));
    ASSERT_EQ(cpu_mask_weight(handle), 1);
    ASSERT_EQ(cpu_mask_first(handle), 69);
    ASSERT_EQ(cpu_mask_next(handle, 69), CPU_MASK_NUM_BITS);

    cpu_mask_handle_free(handle);
}

TEST_CASE(cpu_mask_out_of_line_ops_are_bounded)
{
    struct cpu_mask full = { };
    cpu_mask_handle handle;
    u32 cpu, count = 0;

    g_num_present_cpus = 70;

    /*
     * Bits past the detected CPU count exist in full-size masks, but
     * must be invisible to every bounded operation.
     */
    bit_set(full.bits, 100);
    ASSERT_EQ(cpu_mask_weight(&full), 0);
    ASSERT(cpu_mask_empty(&full));

    cpu_mask_set(&full, 3);
    cpu_mask_set(&full, 69);

    ASSERT_EQ(cpu_mask_handle_alloc(&handle), EOK);
    cpu_mask_copy(handle, &full);
    ASSERT_EQ(cpu_mask_weight(handle), 2);

    for_each_cpu_in(cpu, handle) {
        ASSERT(cpu == 3 || cpu == 69);
        count++;
    }
    ASSERT_EQ(count, 2);

    cpu_mask_handle_free(handle);
}
