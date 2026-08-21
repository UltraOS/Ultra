#include <arch/private/vector_alloc.h>
#include <arch/private/vectors.h>

#include <smp.h>
#include <spinlock.h>
#include <per_cpu.h>
#include <bug.h>

struct vector_pool {
    MAKE_BITMAP(allocated, NUM_DYNAMIC_VECTORS);
    u32 num_allocated;
};

static DEFINE_PER_CPU(struct vector_pool, s_pool);
static struct spinlock s_lock;

error_t vector_alloc(
    const struct cpu_mask *allowed, u32 *out_cpu, u8 *out_vector
)
{
    struct vector_pool *pool, *best_pool = NULL;
    u32 cpu, best_cpu = 0;
    reg_t bit;

    spin_lock(&s_lock);

    for_each_online_cpu(cpu) {
        if (allowed && !cpu_mask_test(allowed, cpu))
            continue;

        pool = per_cpu_ptr(&s_pool, cpu);
        if (pool->num_allocated == NUM_DYNAMIC_VECTORS)
            continue;

        if (best_pool == NULL ||
            pool->num_allocated < best_pool->num_allocated) {
            best_cpu = cpu;
            best_pool = pool;
        }
    }

    if (best_pool == NULL) {
        spin_unlock(&s_lock);
        return ENOSPC;
    }

    pool = best_pool;

    bit = find_first_zero_bit(pool->allocated, NUM_DYNAMIC_VECTORS);
    BUG_ON(bit == NUM_DYNAMIC_VECTORS);

    bit_set(pool->allocated, bit);
    pool->num_allocated++;

    spin_unlock(&s_lock);

    *out_cpu = best_cpu;
    *out_vector = VECTOR_DYNAMIC_FIRST + bit;
    return EOK;
}

void vector_free(u32 cpu, u8 vector)
{
    struct vector_pool *pool;
    reg_t bit;

    BUG_ON(cpu >= g_num_present_cpus);
    BUG_ON(vector < VECTOR_DYNAMIC_FIRST || vector > VECTOR_DYNAMIC_LAST);

    pool = per_cpu_ptr(&s_pool, cpu);
    bit = vector - VECTOR_DYNAMIC_FIRST;

    spin_lock(&s_lock);

    BUG_ON(!bit_test(pool->allocated, bit));
    bit_clear(pool->allocated, bit);
    pool->num_allocated--;

    spin_unlock(&s_lock);
}
