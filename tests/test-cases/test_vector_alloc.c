#include <smp.h>

#include <kernel-source/arch/x86_64/vector_alloc.c>

#include <test_harness.h>

struct cpu_mask g_online_cpus;
ptr_t g_per_cpu_offset[ULTRA_MAX_CPUS];

// Backs every CPU's copy of s_pool through the fake offset table
static struct vector_pool s_fake_pools[ULTRA_MAX_CPUS];

static void reset_state(void)
{
    u32 i;

    memset(s_fake_pools, 0, sizeof(s_fake_pools));
    for (i = 0; i < ULTRA_MAX_CPUS; i++)
        g_per_cpu_offset[i] = (ptr_t)&s_fake_pools[i] - (ptr_t)&s_pool;

    g_num_present_cpus = ULTRA_MAX_CPUS;
    cpu_mask_clear_all(&g_online_cpus);
}

TEST_CASE(vector_alloc_sequential_and_exhaustion)
{
    u32 cpu, i;
    u8 vector, first_vector;

    reset_state();
    cpu_mask_set(&g_online_cpus, 0);

    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 0);
    ASSERT_EQ(vector, VECTOR_DYNAMIC_FIRST);
    first_vector = vector;

    for (i = 1; i < NUM_DYNAMIC_VECTORS; i++) {
        ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
        ASSERT_EQ(cpu, 0);
        ASSERT_EQ(vector, VECTOR_DYNAMIC_FIRST + i);
    }

    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), ENOSPC);

    vector_free(0, first_vector);
    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 0);
    ASSERT_EQ(vector, first_vector);
}

TEST_CASE(vector_alloc_least_busy)
{
    u32 cpu;
    u8 vector;

    reset_state();
    cpu_mask_set(&g_online_cpus, 0);
    cpu_mask_set(&g_online_cpus, 3);
    cpu_mask_set(&g_online_cpus, 100);

    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 0);

    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 3);

    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 100);

    // All equally loaded again, the earliest online CPU wins ties
    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 0);

    vector_free(3, VECTOR_DYNAMIC_FIRST);
    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 3);
}

TEST_CASE(vector_alloc_mask_filter_is_strict)
{
    struct cpu_mask allowed = { };
    u32 cpu, i;
    u8 vector;

    reset_state();
    cpu_mask_set(&g_online_cpus, 0);
    cpu_mask_set(&g_online_cpus, 1);

    cpu_mask_set(&allowed, 1);

    for (i = 0; i < NUM_DYNAMIC_VECTORS; i++) {
        ASSERT_EQ(vector_alloc(&allowed, &cpu, &vector), EOK);
        ASSERT_EQ(cpu, 1);
    }

    /*
     * CPU1 is exhausted. The filter must fail strictly even though
     * CPU0 has plenty of free vectors.
     */
    ASSERT_EQ(vector_alloc(&allowed, &cpu, &vector), ENOSPC);

    ASSERT_EQ(vector_alloc(NULL, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 0);
}

TEST_CASE(vector_alloc_offline_cpus_skipped)
{
    struct cpu_mask allowed = { };
    u32 cpu;
    u8 vector;

    reset_state();
    cpu_mask_set(&g_online_cpus, 0);

    // CPU5 is allowed but not online
    cpu_mask_set(&allowed, 5);
    ASSERT_EQ(vector_alloc(&allowed, &cpu, &vector), ENOSPC);

    cpu_mask_set(&allowed, 0);
    ASSERT_EQ(vector_alloc(&allowed, &cpu, &vector), EOK);
    ASSERT_EQ(cpu, 0);
}
